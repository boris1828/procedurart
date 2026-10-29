#include "core/piece.h"

#include <imgui.h>
#include <raylib.h>
#include <rlImGui.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace core {

// Owns the window, the canvas and the render-on-change logic.
class App {
public:
    explicit App(Piece& piece) : piece_(piece) {}

    int run() {
        SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
        InitWindow(1400, 900, piece_.name());
        SetTargetFPS(60);
        rlImGuiSetup(true);

        canvas_ = std::make_unique<Canvas>(piece_.width(), piece_.height());

        while (!WindowShouldClose()) {
            if (sheet_requested_) export_sheet(); // outside the ImGui frame: it renders offscreen
            if (dirty_) render();

            BeginDrawing();
            ClearBackground({24, 24, 28, 255});
            rlImGuiBegin();
            draw_ui();
            rlImGuiEnd();
            EndDrawing();
        }

        close_viewer();
        canvas_.reset(); // must unload GPU resources before the window closes
        rlImGuiShutdown();
        CloseWindow();
        return 0;
    }

private:
    void render() {
        rng_.reseed(seed_);
        canvas_->begin();
        canvas_->clear({0, 0, 0, 255});
        piece_.render(*canvas_, rng_);
        canvas_->end();
        dirty_ = false;
    }

    // Renders the piece with the current parameters for kSheetN*kSheetN consecutive
    // seeds (starting at the current one, row by row) and saves them as one image.
    void export_sheet() {
        sheet_requested_ = false;
        const int cols = sheet_cols_, rows = sheet_rows_;
        int cw, ch, sw, sh;
        sheet_size(cw, ch, sw, sh);

        // Cells touch: no gutter, every pixel is overwritten below.
        std::vector<uint8_t> sheet(static_cast<size_t>(sw) * static_cast<size_t>(sh) * 4);

        for (int n = 0; n < cols * rows; ++n) {
            rng_.reseed(seed_ + static_cast<uint32_t>(n));
            canvas_->begin();
            canvas_->clear({0, 0, 0, 255});
            piece_.render(*canvas_, rng_);
            canvas_->end();

            const std::vector<uint8_t> cell = canvas_->read_rgba(cw, ch);
            const int x0 = (n % cols) * cw, y0 = (n / cols) * ch;
            for (int y = 0; y < ch; ++y) {
                std::copy_n(cell.begin() + static_cast<size_t>(y) * static_cast<size_t>(cw) * 4,
                            static_cast<size_t>(cw) * 4,
                            sheet.begin() + (static_cast<size_t>(y0 + y) * static_cast<size_t>(sw) + static_cast<size_t>(x0)) * 4);
            }
        }

        std::filesystem::create_directories("exports");
        const std::string path = "exports/" + std::string(piece_.name()) + "_sheet_" +
                                 std::to_string(seed_) + ".png";
        Image img{sheet.data(), sw, sh, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        if (ExportImage(img, path.c_str())) {
            last_export_ = path;
            open_viewer(img, path);
        } else {
            last_export_ = "FAILED: " + path;
        }
        dirty_ = true; // the canvas now holds the last sheet cell: redraw the current seed
    }

    // Cell and sheet size in pixels for the current "sheet size" setting.
    void sheet_size(int& cw, int& ch, int& sw, int& sh) const {
        cw = std::max(1, canvas_->width() * sheet_percent_ / 100);
        ch = std::max(1, canvas_->height() * sheet_percent_ / 100);
        sw = sheet_cols_ * cw;
        sh = sheet_rows_ * ch;
    }

    // --- in-app image viewer for the exported sheet -------------------------
    void open_viewer(const Image& img, const std::string& path) {
        close_viewer();
        viewer_tex_ = LoadTextureFromImage(img);
        GenTextureMipmaps(&viewer_tex_); // smooth when the big sheet is shown small
        SetTextureFilter(viewer_tex_, TEXTURE_FILTER_TRILINEAR);
        viewer_path_ = path;
        viewer_open_ = true;
    }

    void close_viewer() {
        if (viewer_tex_.id != 0) UnloadTexture(viewer_tex_);
        viewer_tex_ = {};
        viewer_open_ = false;
    }

    // Covers the canvas area while open. Returns after drawing; "Delete" removes the file.
    void draw_viewer(float x, float w, float h) {
        ImGui::SetNextWindowPos({x, 0});
        ImGui::SetNextWindowSize({w, h});
        ImGui::Begin("Sheet", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar);
        ImGui::TextUnformatted(viewer_path_.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%dx%d)", viewer_tex_.width, viewer_tex_.height);

        bool close = false;
        if (ImGui::Button("Delete")) {
            std::error_code ec;
            std::filesystem::remove(viewer_path_, ec);
            last_export_ = ec ? "Could not delete " + viewer_path_ : "Deleted " + viewer_path_;
            close = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Open in system viewer")) OpenURL(std::filesystem::absolute(viewer_path_).string().c_str());
        ImGui::SameLine();
        if (ImGui::Button("Close")) close = true;

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float s = std::min(avail.x / static_cast<float>(viewer_tex_.width),
                                 avail.y / static_cast<float>(viewer_tex_.height));
        const float iw = static_cast<float>(viewer_tex_.width) * s, ih = static_cast<float>(viewer_tex_.height) * s;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - iw) * 0.5f);
        rlImGuiImageSize(&viewer_tex_, static_cast<int>(iw), static_cast<int>(ih));
        ImGui::End();

        if (close) close_viewer();
    }

    void draw_ui() {
        const float panel_w = 440.0f;
        const float w = static_cast<float>(GetScreenWidth());
        const float h = static_cast<float>(GetScreenHeight());
        const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoCollapse;

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({panel_w, h});
        ImGui::Begin("Controls", nullptr, fixed);

        ImGui::TextDisabled("%s  (%dx%d)", piece_.name(), canvas_->width(), canvas_->height());
        ImGui::SeparatorText("Seed");
        int seed = static_cast<int>(seed_);
        if (ImGui::InputInt("##seed", &seed)) {
            seed_ = static_cast<uint32_t>(seed);
            dirty_ = true;
        }
        if (ImGui::Button("Random seed")) {
            seed_ = static_cast<uint32_t>(GetRandomValue(0, 0x7fffffff));
            dirty_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Re-render")) dirty_ = true;

        ImGui::SeparatorText("Parameters");
        piece_.set_seed(seed_);
        if (piece_.ui()) dirty_ = true;
        if (piece_.take_seed_request()) {
            seed_ = static_cast<uint32_t>(GetRandomValue(0, 0x7fffffff));
            dirty_ = true;
        }

        ImGui::SeparatorText("Export");
        if (ImGui::Button("Save PNG")) {
            std::filesystem::create_directories("exports");
            last_export_ = "exports/" + std::string(piece_.name()) + "_" +
                           std::to_string(seed_) + ".png";
            if (!canvas_->save_png(last_export_)) last_export_ = "FAILED: " + last_export_;
        }
        ImGui::SetNextItemWidth(190);
        ImGui::SliderInt("Sheet columns", &sheet_cols_, 2, 10);
        ImGui::SetNextItemWidth(190);
        ImGui::SliderInt("Sheet rows", &sheet_rows_, 2, 10);
        ImGui::SetNextItemWidth(190);
        ImGui::SliderInt("Sheet cell size %", &sheet_percent_, 5, 50);
        {
            int cw, ch, sw, sh;
            sheet_size(cw, ch, sw, sh);
            ImGui::TextDisabled("cell %dx%d, sheet %dx%d", cw, ch, sw, sh);
        }
        const std::string export_label =
            "Export " + std::to_string(sheet_cols_) + "x" + std::to_string(sheet_rows_) + " sheet";
        if (ImGui::Button(export_label.c_str())) sheet_requested_ = true;
        ImGui::SameLine();
        ImGui::TextDisabled("seeds %u..%u", seed_, seed_ + static_cast<uint32_t>(sheet_cols_ * sheet_rows_) - 1);
        if (!last_export_.empty()) ImGui::TextWrapped("%s", last_export_.c_str());

        ImGui::End();

        ImGui::SetNextWindowPos({panel_w, 0});
        ImGui::SetNextWindowSize({w - panel_w, h});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 8});
        ImGui::Begin("Canvas", nullptr, fixed | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar);
        canvas_->show_in_imgui();
        ImGui::End();
        if (viewer_open_) draw_viewer(panel_w, w - panel_w, h);
        ImGui::PopStyleVar();
    }

    int sheet_cols_ = 10;
    int sheet_rows_ = 10;

    Piece& piece_;
    std::unique_ptr<Canvas> canvas_;
    bool sheet_requested_ = false;
    int sheet_percent_ = 25; // cell size as a percentage of the canvas size
    bool viewer_open_ = false;
    Texture2D viewer_tex_{};
    std::string viewer_path_;
    Rng rng_;
    uint32_t seed_ = 1;
    bool dirty_ = true;
    std::string last_export_;
};

int run(Piece& piece) { return App(piece).run(); }

} // namespace core
