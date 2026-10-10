// System 32 video (single screen), after MAME 0.289 src/mame/sega/segas32_v.cpp:
// tilemap layers NBG0/1 (zoom) and NBG2/3 (row scroll/select), the text
// layer, the bitmap layer, the line background, the sprite renderer with its
// two frame buffers, and the mixer. BSD-3-Clause (that file's licence).
//
// Layer bitmaps hold palette indices as MAME's do (tile colour * 16 + pen);
// render() mixes them into 0x00RRGGBB.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace segas32 {

class Video32 {
public:
    static constexpr int kMaxWidth = 416, kHeight = 224;

    // The memories the video hardware reads (owned by the board).
    struct Inputs {
        const uint16_t *videoram;      // 0x10000 words
        const uint16_t *spriteram;     // 0x10000 words
        const uint16_t *palette;       // 0x4000 words, xBBBBBGGGGGRRRRR
        const uint16_t *mixer;         // 0x40 words
        const uint8_t *sprite_latched; // 8 bytes
        uint8_t tilebank_external;
        bool display_enable;
    };

    explicit Video32(const std::vector<uint8_t> &tile_rom, const std::vector<uint8_t> &sprite_rom);

    // The board calls this for every video RAM word written, so cached
    // tilemap pages and the text layer are redrawn where they changed
    // (MAME's tilemap dirty marking in videoram_w).
    void videoram_written(uint32_t word_offset);
    void invalidate_all();

    // Sprite engine (MAME update_sprites' erase/swap/render).
    void erase_sprites();
    void swap_sprite_buffers();
    void render_sprite_list(const Inputs &in);
    bool sprite_buffer_select() const { return sprite_num_ < sprite2_num_; } // sprite_control_r(0) bit 0

    // Draws a frame; returns its width (320 or 416).
    int render(const Inputs &in);

    // Threaded rendering: the emulation thread copies what render() reads
    // (at VBLANK start) and a second Video32 draws it on another core.
    struct Snapshot {
        std::vector<uint16_t> videoram, palette, sprites; // sprites: the displayed sprite buffer
        std::array<uint16_t, 0x40> mixer{};
        std::array<uint8_t, 8> latched{};
        uint8_t tilebank_external = 0;
        bool display_enable = false;
    };
    void take_snapshot(const Inputs &in, Snapshot &s) const;
    // On the drawing instance: marks cached tiles dirty where video RAM
    // changed since its last snapshot, takes the sprite buffer, renders.
    int render_snapshot(const Snapshot &s);
    const std::vector<uint32_t> &frame() const { return frame_; }

private:
    enum {
        LAYER_TEXT, LAYER_NBG0, LAYER_NBG1, LAYER_NBG2, LAYER_NBG3, LAYER_BITMAP, LAYER_SPRITES,
        LAYER_BACKGROUND, LAYER_SPRITES_2, LAYER_COUNT
    };
    struct Layer {
        std::vector<uint16_t> bitmap; // kMaxWidth x kHeight
        std::array<uint8_t, 256> transparent{};
        uint16_t *row(int y) { return &bitmap[size_t(y) * kMaxWidth]; }
    };
    struct Rect {
        int min_x, max_x, min_y, max_y;
    };
    struct Extents {
        uint8_t scan_extent[256];
        uint16_t extent[32][16];
    };

    // Tilemap page cache: 512x256 pixmaps of palette indices per (page, bank).
    struct Page {
        std::vector<uint16_t> pix = std::vector<uint16_t>(512 * 256);
        std::array<uint8_t, 512> dirty;
        bool any_dirty = true;
        Page() { dirty.fill(1); }
    };
    const uint16_t *page_pixmap(int page, int bank);
    void draw_page_tile(Page &p, int page, int bank, int tile);
    void update_text_layer();
    uint16_t tile_pixel(int page, int bank, int x, int y) const; // uncached (reference)
    uint16_t text_pixel(int x, int y) const;
    bool compute_clipping_extents(bool enable, bool clipout, int clipmask, const Rect &clip, Extents &list) const;
    void compute_tilemap_flips(int bgnum, bool &flipx, bool &flipy) const;
    void get_tilemaps(int bgnum, int pages[4], int &bank) const;
    void update_tilemap_zoom(Layer &layer, const Rect &clip, int bgnum);
    void update_tilemap_rowscroll(Layer &layer, const Rect &clip, int bgnum);
    void update_tilemap_text(Layer &layer, const Rect &clip);
    void update_bitmap(Layer &layer, const Rect &clip);
    void update_background(Layer &layer, const Rect &clip);
    uint8_t update_tilemaps(const Rect &clip);
    int draw_one_sprite(const uint16_t *data, int xoffs, int yoffs, const Rect &clipin, const Rect &clipout);
    uint32_t sprite_dword(bool fromram, uint32_t index) const;
    void mix_all_layers(const Rect &clip, uint8_t enablemask);
    const uint16_t *layer_scanline(int layer, int y);

    std::vector<uint8_t> tiles_;          // decoded 16x16 tiles, one pen per byte
    std::array<std::unique_ptr<Page>, 128 * 4> pages_;
    int pages_alive_ = 0;
    std::vector<uint16_t> text_pix_ = std::vector<uint16_t>(512 * 256);
    std::array<uint8_t, 2048> text_cell_dirty_;
    std::array<uint8_t, 4096> text_char_dirty_;
    bool text_all_dirty_ = true;
    const std::vector<uint8_t> &sprite_rom_;
    std::array<Layer, LAYER_COUNT> layers_;
    int sprite_num_ = LAYER_SPRITES, sprite2_num_ = LAYER_SPRITES_2; // MAME layer "num", swapped with the buffers
    std::array<uint16_t, 512> solid_0000_{}, solid_ffff_{};
    std::vector<uint32_t> frame_;
    Inputs in_{};
    int width_ = 416;
    std::vector<uint16_t> last_videoram_; // render_snapshot: video RAM of the previous snapshot
};

} // namespace segas32
