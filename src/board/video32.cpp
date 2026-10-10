#include "board/video32.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace segas32 {

namespace {

constexpr int sext(uint32_t value, int bits) { return int32_t(value << (32 - bits)) >> (32 - bits); }

// MAME bgcharlayout: 16x16, 4 planes at bit offsets 0-3 of each nibble,
// x offsets { 0,4,16,20,8,12,24,28,32,36,48,52,40,44,56,60 }, 64 bits a row.
constexpr int kBgX[16] = {0, 4, 16, 20, 8, 12, 24, 28, 32, 36, 48, 52, 40, 44, 56, 60};

} // namespace

Video32::Video32(const std::vector<uint8_t> &tile_rom, const std::vector<uint8_t> &sprite_rom)
    : sprite_rom_(sprite_rom), frame_(size_t(kMaxWidth) * kHeight) {
    const size_t count = tile_rom.size() / 128;
    tiles_.resize(count * 256);
    for (size_t t = 0; t < count; ++t)
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const size_t bit = t * 1024 + size_t(y) * 64 + size_t(kBgX[x]); // MSB-first bit order
                const uint8_t byte = tile_rom[bit / 8];
                tiles_[t * 256 + size_t(y) * 16 + size_t(x)] = uint8_t((bit & 4) ? byte & 0x0f : byte >> 4);
            }
    for (auto &l : layers_) l.bitmap.assign(size_t(kMaxWidth) * kHeight, 0); // MAME bitmaps start cleared
    solid_ffff_.fill(0xffff);
}

// --- tilemaps --------------------------------------------------------------------

// MAME get_tile_info on a 32x16-tile page (512x256 pixels): gfx 0, code
// (bank << 13) | (data & 0x1fff), colour (data >> 4) & 0x1ff, flips data >> 14.
uint16_t Video32::tile_pixel(int page, int bank, int x, int y) const {
    const uint16_t data = in_.videoram[((page & 0x7f) << 9) | ((y >> 4) << 5) | (x >> 4)];
    const size_t code = (size_t(bank) << 13 | (data & 0x1fff)) % (tiles_.size() / 256);
    int tx = x & 15, ty = y & 15;
    if (data & 0x4000) tx ^= 15;
    if (data & 0x8000) ty ^= 15;
    return uint16_t(((data >> 4) & 0x1ff) * 16 + tiles_[code * 256 + size_t(ty) * 16 + size_t(tx)]);
}

// Text layer: 64x32 tiles of 8x8, gfx_8x8x4_packed_msb from video RAM.
uint16_t Video32::text_pixel(int x, int y) const {
    const uint16_t ctrl = in_.videoram[0x1ff5c / 2];
    const uint16_t data = in_.videoram[(((ctrl >> 4) & 0x1f) << 11) | ((y >> 3) << 6) | (x >> 3)];
    const uint32_t code = uint32_t((ctrl & 7) << 9) | (data & 0x1ff);
    const uint32_t byte_addr = code * 32 + uint32_t(y & 7) * 4 + uint32_t(x & 7) / 2;
    const uint16_t word = in_.videoram[(byte_addr >> 1) & 0xffff];
    const uint8_t byte = uint8_t((byte_addr & 1) ? word >> 8 : word);
    const int pen = (x & 1) ? byte & 0x0f : byte >> 4;
    return pen ? uint16_t(((data >> 9) & 0x7f) * 16 + pen) : 0;
}

bool Video32::compute_clipping_extents(bool enable, bool clipout, int clipmask, const Rect &cliprect, Extents &list) const {
    const bool flip = (in_.videoram[0x1ff00 / 2] >> 9) & 1;
    Rect tempclip = cliprect;
    tempclip.max_x++;
    tempclip.max_y++;
    list.extent[0][0] = uint16_t(tempclip.min_x);
    list.extent[0][1] = uint16_t(tempclip.max_x);
    if (!enable) {
        std::memset(&list.scan_extent[tempclip.min_y], 0, size_t(tempclip.max_y - tempclip.min_y));
        return true;
    }
    Rect clips[5];
    int sorted[5];
    for (int i = 0; i < 5; i++) {
        const uint16_t *v = in_.videoram;
        if (!flip) {
            clips[i].min_x = v[0x1ff60 / 2 + i * 4] & 0x1ff;
            clips[i].min_y = v[0x1ff62 / 2 + i * 4] & 0x0ff;
            clips[i].max_x = (v[0x1ff64 / 2 + i * 4] & 0x1ff) + 1;
            clips[i].max_y = (v[0x1ff66 / 2 + i * 4] & 0x0ff) + 1;
        } else {
            const int vmax_x = width_ - 1, vmax_y = kHeight - 1;
            clips[i].max_x = (vmax_x + 1) - (v[0x1ff60 / 2 + i * 4] & 0x1ff);
            clips[i].max_y = (vmax_y + 1) - (v[0x1ff62 / 2 + i * 4] & 0x0ff);
            clips[i].min_x = (vmax_x + 1) - ((v[0x1ff64 / 2 + i * 4] & 0x1ff) + 1);
            clips[i].min_y = (vmax_y + 1) - ((v[0x1ff66 / 2 + i * 4] & 0x0ff) + 1);
        }
        // rectangle &= (MAME: intersection, may become empty)
        clips[i].min_x = std::max(clips[i].min_x, tempclip.min_x);
        clips[i].max_x = std::min(clips[i].max_x, tempclip.max_x);
        clips[i].min_y = std::max(clips[i].min_y, tempclip.min_y);
        clips[i].max_y = std::min(clips[i].max_y, tempclip.max_y);
        sorted[i] = i;
    }
    for (int i = 0; i < 5; i++)
        for (int j = i + 1; j < 5; j++)
            if (clips[sorted[i]].min_x > clips[sorted[j]].min_x) std::swap(sorted[i], sorted[j]);
    for (int i = 1; i < 32; i++) {
        if (i & clipmask) {
            uint16_t *extent = &list.extent[i][0];
            *extent++ = uint16_t(tempclip.min_x);
            for (int j = 0; j < 5; j++) {
                if ((i >> sorted[j]) & 1) {
                    const Rect &cur = clips[sorted[j]];
                    if (extent != &list.extent[i][1] && cur.min_x <= extent[-1]) {
                        if (cur.max_x > extent[-1]) extent[-1] = uint16_t(cur.max_x);
                    } else {
                        *extent++ = uint16_t(cur.min_x);
                        *extent++ = uint16_t(cur.max_x);
                    }
                }
            }
            *extent++ = uint16_t(tempclip.max_x);
        }
    }
    for (int y = tempclip.min_y; y < tempclip.max_y; y++) {
        int sect = 0;
        for (int i = 0; i < 5; i++)
            if (((clipmask >> i) & 1) && y >= clips[i].min_y && y < clips[i].max_y) sect |= 1 << i;
        list.scan_extent[y] = uint8_t(sect);
    }
    return clipout;
}

void Video32::compute_tilemap_flips(int bgnum, bool &flipx, bool &flipy) const {
    const uint16_t c = in_.videoram[0x1ff00 / 2];
    const bool global_flip = (c >> 9) & 1;
    const bool layer_flip = (c >> bgnum) & 1;
    const bool prohibit_flipy = (c >> 8) & 1;
    flipx = layer_flip ? !global_flip : global_flip;
    flipy = (layer_flip && !prohibit_flipy) ? !global_flip : global_flip;
}

void Video32::get_tilemaps(int bgnum, int pages[4], int &bank) const {
    const uint16_t *v = in_.videoram;
    bank = ((in_.tilebank_external & 1) << 1) | ((v[0x1ff00 / 2] & 0x400) >> 10);
    pages[0] = (v[0x1ff40 / 2 + 2 * bgnum + 0] >> 0) & 0x7f;
    pages[1] = (v[0x1ff40 / 2 + 2 * bgnum + 0] >> 8) & 0x7f;
    pages[2] = (v[0x1ff40 / 2 + 2 * bgnum + 1] >> 0) & 0x7f;
    pages[3] = (v[0x1ff40 / 2 + 2 * bgnum + 1] >> 8) & 0x7f;
}

void Video32::update_tilemap_zoom(Layer &layer, const Rect &cliprect, int bgnum) {
    const uint16_t *v = in_.videoram;
    int pages[4], bank;
    get_tilemaps(bgnum, pages, bank);
    bool flipx, flipy;
    compute_tilemap_flips(bgnum, flipx, flipy);

    const bool clipenable = (v[0x1ff02 / 2] >> (11 + bgnum)) & 1;
    const bool clipout = (v[0x1ff02 / 2] >> (6 + bgnum)) & 1;
    const int clips = (v[0x1ff06 / 2] >> (4 * bgnum)) & 0x0f;
    Extents clip_extents;
    const bool clipdraw_start = compute_clipping_extents(clipenable, clipout, clips, cliprect, clip_extents);

    int dstxstep = v[0x1ff50 / 2 + 2 * bgnum] & 0xfff;
    int dstystep = ((v[0x1ff00 / 2] >> 14) & 1) ? (v[0x1ff52 / 2 + 2 * bgnum] & 0xfff) : dstxstep;
    if (dstxstep < 0x80) dstxstep = 0x80;
    if (dstystep < 0x80) dstystep = 0x80;
    uint32_t srcxstep = (0x200u << 20) / uint32_t(dstxstep);
    uint32_t srcystep = (0x200u << 20) / uint32_t(dstystep);

    uint32_t srcx_start = uint32_t(v[0x1ff12 / 2 + 4 * bgnum] & 0x3ff) << 20;
    srcx_start += uint32_t(v[0x1ff10 / 2 + 4 * bgnum] & 0xff00) << 4;
    uint32_t srcy = uint32_t(v[0x1ff16 / 2 + 4 * bgnum] & 0x1ff) << 20;
    srcy += uint32_t(v[0x1ff14 / 2 + 4 * bgnum] & 0xfe00) << 4;
    srcx_start -= uint32_t(sext(v[0x1ff30 / 2 + 2 * bgnum], (dstxstep != 0x200) ? 10 : 9)) * srcxstep;
    srcy -= uint32_t(sext(v[0x1ff32 / 2 + 2 * bgnum], (dstystep != 0x200) ? 10 : 9)) * srcystep;
    srcx_start += uint32_t(cliprect.min_x) * srcxstep;
    srcy += uint32_t(cliprect.min_y) * srcystep;
    if (flipy) {
        srcy += uint32_t((kHeight - 1) - 2 * cliprect.min_y) * srcystep;
        srcystep = uint32_t(-int32_t(srcystep));
    }
    if (flipx) {
        srcx_start += uint32_t((width_ - 1) - 2 * cliprect.min_x) * srcxstep;
        srcxstep = uint32_t(-int32_t(srcxstep));
    }

    for (int y = cliprect.min_y; y <= cliprect.max_y; y++) {
        const uint16_t *extents = &clip_extents.extent[clip_extents.scan_extent[y]][0];
        uint16_t *const dst = layer.row(y);
        bool clipdraw = clipdraw_start;
        if (clipdraw || extents[1] <= cliprect.max_x) {
            int transparent = 0;
            const int tm0 = pages[((srcy >> 27) & 2) + 0], tm1 = pages[((srcy >> 27) & 2) + 1];
            const int row = int((srcy >> 20) & 0xff);
            uint32_t srcx = srcx_start;
            for (;;) {
                if (clipdraw) {
                    for (int x = extents[0]; x < extents[1]; x++) {
                        uint16_t pix = tile_pixel((srcx >> 29) & 1 ? tm1 : tm0, bank, int((srcx >> 20) & 0x1ff), row);
                        srcx += srcxstep;
                        if ((pix & 0x0f) == 0) pix = 0, transparent++;
                        dst[x] = pix;
                    }
                } else {
                    const int pixels = extents[1] - extents[0];
                    std::fill_n(&dst[extents[0]], pixels, 0);
                    srcx += srcxstep * uint32_t(pixels);
                    transparent += pixels;
                }
                if (extents[1] > cliprect.max_x) break;
                clipdraw = !clipdraw;
                extents++;
            }
            layer.transparent[y] = (transparent == cliprect.max_x - cliprect.min_x + 1);
        } else
            layer.transparent[y] = 1;
        srcy += srcystep;
    }
}

void Video32::update_tilemap_rowscroll(Layer &layer, const Rect &cliprect, int bgnum) {
    const uint16_t *v = in_.videoram;
    int pages[4], bank;
    get_tilemaps(bgnum, pages, bank);
    bool flipx, flipy;
    compute_tilemap_flips(bgnum, flipx, flipy);

    const bool clipenable = (v[0x1ff02 / 2] >> (11 + bgnum)) & 1;
    const bool clipout = (v[0x1ff02 / 2] >> (6 + bgnum)) & 1;
    const int clips = (v[0x1ff06 / 2] >> (4 * bgnum)) & 0x0f;
    Extents clip_extents;
    const bool clipdraw_start = compute_clipping_extents(clipenable, clipout, clips, cliprect, clip_extents);

    bool rowscroll = (v[0x1ff04 / 2] >> (bgnum - 2)) & 1;
    bool rowselect = (v[0x1ff04 / 2] >> bgnum) & 1;
    if ((v[0x1ff04 / 2] >> (bgnum + 2)) & 1) rowscroll = rowselect = false;
    const uint16_t *const table = &v[(v[0x1ff04 / 2] >> 10) * 0x400];
    const int xscroll = (v[0x1ff12 / 2 + 4 * bgnum] & 0x3ff) - (v[0x1ff30 / 2 + 2 * bgnum] & 0x1ff);
    const int yscroll = (v[0x1ff16 / 2 + 4 * bgnum] & 0x1ff);

    for (int y = cliprect.min_y; y <= cliprect.max_y; y++) {
        const uint16_t *extents = &clip_extents.extent[clip_extents.scan_extent[y]][0];
        uint16_t *const dst = layer.row(y);
        bool clipdraw = clipdraw_start;
        if (clipdraw || extents[1] <= cliprect.max_x) {
            int transparent = 0;
            int srcx, srcxstep;
            if (!flipx) srcx = cliprect.min_x + xscroll, srcxstep = 1;
            else srcx = cliprect.max_x + xscroll, srcxstep = -1;
            int srcy, ylookup;
            if (!flipy) srcy = yscroll + y, ylookup = y;
            else srcy = yscroll + (kHeight - 1) - y, ylookup = (kHeight - 1) - y;
            if (rowscroll) srcx += table[0x000 + 0x100 * (bgnum - 2) + ylookup] & 0x3ff;
            if (rowselect) srcy = (yscroll + table[0x200 + 0x100 * (bgnum - 2) + ylookup]) & 0x1ff;
            const int tm0 = pages[((srcy >> 7) & 2) + 0], tm1 = pages[((srcy >> 7) & 2) + 1];
            const int row = srcy & 0xff;
            for (;;) {
                if (clipdraw) {
                    for (int x = extents[0]; x < extents[1]; x++, srcx += srcxstep) {
                        uint16_t pix = tile_pixel((srcx >> 9) & 1 ? tm1 : tm0, bank, srcx & 0x1ff, row);
                        if ((pix & 0x0f) == 0) pix = 0, transparent++;
                        dst[x] = pix;
                    }
                } else {
                    const int pixels = extents[1] - extents[0];
                    std::fill_n(&dst[extents[0]], pixels, 0);
                    srcx += srcxstep * pixels;
                    transparent += pixels;
                }
                if (extents[1] > cliprect.max_x) break;
                clipdraw = !clipdraw;
                extents++;
            }
            layer.transparent[y] = (transparent == cliprect.max_x - cliprect.min_x + 1);
        } else
            layer.transparent[y] = 1;
    }
}

// MAME draws the 512x256 text tilemap unscrolled, flipped about its own size
// when the screen is flipped; transparent pen 0.
void Video32::update_tilemap_text(Layer &layer, const Rect &cliprect) {
    const bool flip = (in_.videoram[0x1ff00 / 2] >> 9) & 1;
    for (int y = cliprect.min_y; y <= cliprect.max_y; y++) {
        uint16_t *dst = layer.row(y);
        for (int x = cliprect.min_x; x <= cliprect.max_x; x++)
            dst[x] = flip ? text_pixel(511 - x, 255 - y) : text_pixel(x, y);
    }
    // MAME's text layer never marks rows transparent (they stay 0 from start).
}

void Video32::update_bitmap(Layer &layer, const Rect &cliprect) {
    const uint16_t *v = in_.videoram;
    const int bpp = ((v[0x1ff00 / 2] >> 11) & 1) ? 8 : 4;
    const bool clipenable = (v[0x1ff02 / 2] >> 15) & 1;
    const bool clipout = (v[0x1ff02 / 2] >> 10) & 1;
    Extents clip_extents;
    const bool clipdraw_start = compute_clipping_extents(clipenable, clipout, 0x10, cliprect, clip_extents);
    const int xscroll = v[0x1ff88 / 2] & 0x1ff;
    const int yscroll = v[0x1ff8a / 2] & 0x1ff;
    const int color = (v[0x1ff8c / 2] << 4) & 0x1fff0 & ~((1 << bpp) - 1);

    for (int y = cliprect.min_y; y <= cliprect.max_y; y++) {
        const uint16_t *extents = &clip_extents.extent[clip_extents.scan_extent[y]][0];
        uint16_t *const dst = layer.row(y);
        bool clipdraw = clipdraw_start;
        if (clipdraw || extents[1] <= cliprect.max_x) {
            int transparent = 0;
            for (;;) {
                if (clipdraw) {
                    if (bpp == 8) {
                        const uint16_t *src = &v[512 / 2 * ((y + yscroll) & 0xff)];
                        for (int x = extents[0]; x < extents[1]; x++) {
                            const int effx = (x + xscroll) & 0x1ff;
                            const int byte = (effx & 1) ? src[effx >> 1] >> 8 : src[effx >> 1] & 0xff;
                            int pix = byte + color;
                            if ((pix & 0xff) == 0) pix = 0, transparent++;
                            dst[x] = uint16_t(pix);
                        }
                    } else {
                        const uint16_t *src = &v[512 / 4 * ((y + yscroll) & 0x1ff)];
                        for (int x = extents[0]; x < extents[1]; x++) {
                            const int effx = (x + xscroll) & 0x1ff;
                            int pix = ((src[effx / 4] >> (4 * (effx & 3))) & 0x0f) + color;
                            if ((pix & 0x0f) == 0) pix = 0, transparent++;
                            dst[x] = uint16_t(pix);
                        }
                    }
                } else {
                    const int pixels = extents[1] - extents[0];
                    std::fill_n(&dst[extents[0]], pixels, 0);
                    transparent += pixels;
                }
                if (extents[1] > cliprect.max_x) break;
                clipdraw = !clipdraw;
                extents++;
            }
            layer.transparent[y] = (transparent == cliprect.max_x - cliprect.min_x + 1);
        } else
            layer.transparent[y] = 1;
    }
}

void Video32::update_background(Layer &layer, const Rect &cliprect) {
    const uint16_t c = in_.videoram[0x1ff5e / 2];
    for (int y = cliprect.min_y; y <= cliprect.max_y; y++) {
        int color;
        if ((c >> 15) & 1) color = (c & 0x1e00) + ((c + y) & 0x1ff); // line colour select
        else color = c & 0x1e00;
        std::fill_n(layer.row(y) + cliprect.min_x, cliprect.max_x - cliprect.min_x + 1, uint16_t(color));
    }
}

uint8_t Video32::update_tilemaps(const Rect &clip) {
    const uint16_t *v = in_.videoram;
    auto bit = [](uint16_t w, int n) { return (w >> n) & 1; };
    const int enable0 = !bit(v[0x1ff02 / 2], 0) && !bit(v[0x1ff8e / 2], 1);
    const int enable1 = !bit(v[0x1ff02 / 2], 1) && !bit(v[0x1ff8e / 2], 2);
    const int enable2 = !bit(v[0x1ff02 / 2], 2) && !bit(v[0x1ff8e / 2], 3) && !bit(v[0x1ff00 / 2], 12);
    const int enable3 = !bit(v[0x1ff02 / 2], 3) && !bit(v[0x1ff8e / 2], 4) && !bit(v[0x1ff00 / 2], 13);
    const int enablet = !bit(v[0x1ff02 / 2], 4) && !bit(v[0x1ff8e / 2], 0);
    const int enableb = !bit(v[0x1ff02 / 2], 5) && !bit(v[0x1ff8e / 2], 5);
    if (enable0) update_tilemap_zoom(layers_[LAYER_NBG0], clip, 0);
    if (enable1) update_tilemap_zoom(layers_[LAYER_NBG1], clip, 1);
    if (enable2) update_tilemap_rowscroll(layers_[LAYER_NBG2], clip, 2);
    if (enable3) update_tilemap_rowscroll(layers_[LAYER_NBG3], clip, 3);
    if (enablet) update_tilemap_text(layers_[LAYER_TEXT], clip);
    if (enableb) update_bitmap(layers_[LAYER_BITMAP], clip);
    update_background(layers_[LAYER_BACKGROUND], clip);
    return uint8_t((enablet << 0) | (enable0 << 1) | (enable1 << 2) | (enable2 << 3) | (enable3 << 4) | (enableb << 5));
}

// --- sprites -------------------------------------------------------------------------

void Video32::erase_sprites() { std::fill(layers_[LAYER_SPRITES].bitmap.begin(), layers_[LAYER_SPRITES].bitmap.end(), 0xffff); }

void Video32::swap_sprite_buffers() {
    std::swap(layers_[LAYER_SPRITES], layers_[LAYER_SPRITES_2]);
    std::swap(sprite_num_, sprite2_num_);
}

// Sprite data as MAME's 32-bit views: the ROM region is big-endian dwords;
// sprite RAM as spriteram_w packs m_spriteram_32bit.
uint32_t Video32::sprite_dword(bool fromram, uint32_t i) const {
    if (fromram) {
        const uint16_t even = in_.spriteram[(i * 2) & 0xffff], odd = in_.spriteram[(i * 2 + 1) & 0xffff];
        return ((odd >> 8) & 0x000000ff) | ((odd << 8) & 0x0000ff00) | ((uint32_t(even) << 8) & 0x00ff0000) |
               ((uint32_t(even) << 24) & 0xff000000);
    }
    const uint8_t *p = &sprite_rom_[size_t(i) * 4];
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

int Video32::draw_one_sprite(const uint16_t *data, int xoffs, int yoffs, const Rect &clipin, const Rect &clipout) {
    static const int transparency_masks[4][4] = {{0x7fff, 0x3fff, 0x1fff, 0x0fff},
                                                 {0x3fff, 0x1fff, 0x0fff, 0x07ff},
                                                 {0x3fff, 0x1fff, 0x0fff, 0x07ff},
                                                 {0x1fff, 0x0fff, 0x07ff, 0x03ff}};
    Layer &layer = layers_[LAYER_SPRITES_2];
    const uint8_t *latched = in_.sprite_latched;
    const int numbanks = int(sprite_rom_.size() / 4) >> 20;

    const bool indirect = (data[0] >> 13) & 1;
    const bool indlocal = (data[0] >> 12) & 1;
    const bool shadow = ((data[0] >> 11) & 1) && (latched[0x0a / 2] & 1);
    const bool fromram = (data[0] >> 10) & 1;
    const bool bpp8 = (data[0] >> 9) & 1;
    const int transp = ((data[0] >> 8) & 1) ? 0 : (bpp8 ? 0xff : 0x0f);
    const bool flipy = (data[0] >> 7) & 1;
    const bool flipx = (data[0] >> 6) & 1;
    const bool offsety = (data[0] >> 5) & 1;
    const bool offsetx = (data[0] >> 4) & 1;
    const int adjusty = (data[0] >> 2) & 3;
    const int adjustx = (data[0] >> 0) & 3;
    const int srch = (data[1] >> 8);
    const int srcw = bpp8 ? (data[1] & 0x3f) : ((data[1] >> 1) & 0x3f);
    int bank = ((data[3] & 0x0800) >> 11) | ((data[3] & 0x4000) >> 13);
    const int dsth = data[2] & 0x3ff;
    const int dstw = data[3] & 0x3ff;
    int ypos = sext(data[4], 12);
    int xpos = sext(data[5], 12);
    uint32_t addr = data[6] | ((data[2] & 0xf000u) << 4);
    const int color = 0x8000 | (data[7] & (bpp8 ? 0x7f00 : 0x7ff0));
    int xdelta = 1, ydelta = 1, yacc = 0, pix, transmask;
    uint32_t base, addrmask;
    uint16_t indtable[16]{};

    if (srcw == 0 || srch == 0 || dstw == 0 || dsth == 0) return (indirect && indlocal) ? 2 : 0;

    transmask = transparency_masks[latched[0x08 / 2] & 3][latched[0x0a / 2] & 3];
    if (bpp8) transmask &= 0xfff0;
    if (indirect) {
        const uint16_t *src = indlocal ? &data[8] : &in_.spriteram[8 * (data[7] & 0x1fff)];
        for (int x = 0; x < 16; x++)
            indtable[x] = uint16_t((src[x] & (bpp8 ? 0xfff0 : 0xffff)) | ((latched[0x0a / 2] & 1) ? 0x8000 : 0x0000));
    }
    if (fromram) {
        base = 0;
        addrmask = (0x20000 / 4) - 1;
    } else {
        if (numbanks) bank %= numbanks;
        base = uint32_t(bank) << 20;
        addrmask = 0xfffff;
    }

    const int hzoom = (((bpp8 ? 4 : 8) * srcw) << 16) / dstw;
    const int vzoom = (srch << 16) / dsth;
    if (offsetx) xpos += xoffs;
    switch (adjustx) {
    case 0: case 3: xpos -= (dstw - 1) / 2; break;
    case 1: xpos -= dstw - 1; break;
    case 2: break;
    }
    if (offsety) ypos += yoffs;
    switch (adjusty) {
    case 0: case 3: ypos -= (dsth - 1) / 2; break;
    case 1: ypos -= dsth - 1; break;
    case 2: break;
    }
    if (flipx) xpos += dstw - 1, xdelta = -1;
    if (flipy) ypos += dsth - 1, ydelta = -1;
    int xtarget = xpos + xdelta * dstw;
    const int ytarget = ypos + ydelta * dsth;
    if (xdelta > 0 && xtarget > clipin.max_x) {
        xtarget = clipin.max_x + 1;
        if (xpos >= xtarget) return (indirect && indlocal) ? 2 : 0;
    }
    if (xdelta < 0 && xtarget < clipin.min_x) {
        xtarget = clipin.min_x - 1;
        if (xpos <= xtarget) return (indirect && indlocal) ? 2 : 0;
    }

    for (int y = ypos; y != ytarget; y += ydelta) {
        if (y >= clipin.min_y && y <= clipin.max_y) {
            const bool do_clipout = (y >= clipout.min_y && y <= clipout.max_y);
            uint16_t *const dest = layer.row(y);
            int xacc = 0;
            auto plot = [&](int x, int p, int trans, bool is8) {
                if (x >= clipin.min_x && x <= clipin.max_x && (!do_clipout || x < clipout.min_x || x > clipout.max_x) &&
                    p != trans) {
                    if (!indirect) {
                        if (p != 0) {
                            if (!shadow) dest[x] = uint16_t(color | p);
                            else dest[x] &= 0x7fff;
                        }
                    } else {
                        const int indpix = is8 ? (indtable[p >> 4] | (p & 0x0f)) : indtable[p];
                        if ((indpix & transmask) != transmask) {
                            if (!shadow) dest[x] = uint16_t(indpix);
                            else dest[x] &= 0x7fff;
                        }
                    }
                }
            };
            uint32_t curaddr = addr - 1;
            for (int x = xpos; x != xtarget;) {
                const uint32_t pixels = sprite_dword(fromram, base + (++curaddr & addrmask));
                const int n = bpp8 ? 4 : 8, bits = bpp8 ? 8 : 4, mask = bpp8 ? 0xff : 0x0f;
                for (int k = 0; k < n; ++k) {
                    pix = int(pixels >> (32 - bits * (k + 1))) & mask;
                    const int trans = (k == 0 || k == n - 1) ? transp : 0;
                    while (xacc < 0x10000 && x != xtarget) {
                        plot(x, pix, trans, bpp8);
                        x += xdelta;
                        xacc += hzoom;
                    }
                    xacc -= 0x10000;
                }
                if (transp != 0 && pix == mask) break; // end code
            }
        }
        yacc += vzoom;
        addr += uint32_t(srcw * (yacc >> 16));
        yacc &= 0xffff;
    }
    return (indirect && indlocal) ? 2 : 0;
}

void Video32::render_sprite_list(const Inputs &in) {
    in_ = in;
    const uint8_t *latched = in.sprite_latched;
    Rect outerclip{0, (latched[0x0c / 2] & 1) ? 415 : 319, 0, 223};
    Rect clipin = outerclip;
    Rect clipout{0, -1, 0, -1};
    int xoffs = 0, yoffs = 0, numentries = 0, spritenum = 0;
    while (numentries++ < 0x20000 / 16) {
        const uint16_t *const sprite = &in.spriteram[8 * (spritenum & 0x1fff)];
        switch (sprite[0] >> 14) {
        case 0:
            spritenum += 1 + draw_one_sprite(sprite, xoffs, yoffs, clipin, clipout);
            break;
        case 1:
            if ((sprite[0] >> 12) & 1) {
                clipin.min_y = sext(sprite[0], 12);
                clipin.max_y = sext(sprite[1], 12);
                clipin.min_x = sext(sprite[2], 12);
                clipin.max_x = sext(sprite[3], 12);
                clipin.min_x = std::max(clipin.min_x, outerclip.min_x);
                clipin.max_x = std::min(clipin.max_x, outerclip.max_x);
                clipin.min_y = std::max(clipin.min_y, outerclip.min_y);
                clipin.max_y = std::min(clipin.max_y, outerclip.max_y);
            }
            if ((sprite[0] >> 13) & 1) {
                clipout.min_y = sext(sprite[4], 12);
                clipout.max_y = sext(sprite[5], 12);
                clipout.min_x = sext(sprite[6], 12);
                clipout.max_x = sext(sprite[7], 12);
            }
            spritenum++;
            break;
        case 2:
            if ((sprite[0] >> 13) & 1) {
                yoffs = sext(sprite[1], 12);
                xoffs = sext(sprite[2], 12);
            }
            spritenum = sprite[0] & 0x1fff;
            break;
        case 3:
            numentries = 0x20000 / 16;
            break;
        }
    }
}

// --- mixer ---------------------------------------------------------------------------

const uint16_t *Video32::layer_scanline(int layer, int y) {
    if (layers_[layer].transparent[y]) return layer == LAYER_SPRITES ? solid_ffff_.data() : solid_0000_.data();
    return layers_[layer].row(y);
}

namespace {

uint8_t compute_color_offsets(const uint16_t *mixer, int layerbit, int layerflag) {
    const int mode = ((mixer[0x3e / 2] & 0x8000) >> 14) | (layerbit & 1);
    switch (mode) {
    case 1: return 2; // grayscale modes (MAME: fix me)
    case 2: return (!layerflag) ? 2 : 0;
    default: return !layerflag;
    }
}

uint16_t compute_sprite_blend(uint8_t encoding) {
    const int value = encoding & 0xf;
    switch ((encoding >> 4) & 3) {
    case 0: return uint16_t(1 << value);
    case 1: return uint16_t((1 << value) | ((1 << value) - 1));
    case 2: return uint16_t(~((1 << value) - 1) & 0xffff);
    default: return 0xffff;
    }
}

} // namespace

void Video32::mix_all_layers(const Rect &cliprect, uint8_t enablemask) {
    const uint16_t *mixer = in_.mixer;
    const bool blendenable = (mixer[0x4e / 2] >> 11) & 1;
    const int blendfactor = (mixer[0x4e / 2] >> 8) & 7;
    struct Info {
        uint16_t palbase, sprblendmask;
        uint8_t blendmask, index, effpri, mixshift, coloroffs;
    } layerorder[16][8], layersort[8];

    int rgboffs[3][3] = {{sext(mixer[0x40 / 2], 6), sext(mixer[0x42 / 2], 6), sext(mixer[0x44 / 2], 6)},
                         {sext(mixer[0x46 / 2], 6), sext(mixer[0x48 / 2], 6), sext(mixer[0x4a / 2], 6)},
                         {0, 0, 0}};

    static const uint8_t groups[16][3] = {{14, 0x00, 0x01}, {14, 0x01, 0x02}, {13, 0x03, 0x04}, {12, 0x07, 0x08},
                                          {14, 0x01, 0x00}, {13, 0x03, 0x00}, {12, 0x07, 0x00}, {11, 0x0f, 0x00},
                                          {14, 0x01, 0x00}, {13, 0x03, 0x00}, {12, 0x07, 0x00}, {11, 0x0f, 0x00},
                                          {13, 0x01, 0x00}, {12, 0x03, 0x00}, {11, 0x07, 0x00}, {10, 0x0f, 0x00}};
    const uint8_t sprgroup_shift = groups[mixer[0x4c / 2] & 0x0f][0];
    const uint8_t sprgroup_mask = groups[mixer[0x4c / 2] & 0x0f][1];
    const uint8_t sprgroup_or = groups[mixer[0x4c / 2] & 0x0f][2];
    const int sprshadowmask = ((mixer[0x4c / 2] >> 2) & 1) ? 0x8000 : 0x0000;
    const int sprpixmask = ((1 << sprgroup_shift) - 1) & 0x3fff;
    const int sprshadow = 0x7ffe & sprpixmask;

    int numlayers = 0;
    for (int laynum = LAYER_TEXT; laynum <= LAYER_BITMAP; laynum++) {
        const int priority = mixer[0x20 / 2 + laynum] & 0x0f;
        if (((enablemask >> laynum) & 1) && priority != 0) {
            Info &l = layersort[numlayers++];
            l.index = uint8_t(laynum);
            l.effpri = uint8_t((priority << 3) | (6 - laynum));
            l.palbase = uint16_t((mixer[0x20 / 2 + laynum] & 0x00f0) << 6);
            l.mixshift = uint8_t((mixer[0x20 / 2 + laynum] >> 8) & 3);
            l.blendmask = blendenable ? uint8_t((mixer[0x30 / 2 + laynum] >> 6) & 0xff) : 0;
            l.sprblendmask = compute_sprite_blend(uint8_t(mixer[0x30 / 2 + laynum] & 0x3f));
            l.coloroffs = compute_color_offsets(mixer, (mixer[0x3e / 2] >> laynum) & 1, (mixer[0x30 / 2 + laynum] >> 14) & 1);
        }
    }
    {
        Info &l = layersort[numlayers++];
        l.index = LAYER_BACKGROUND;
        l.effpri = (1 << 3) | 0;
        l.palbase = uint16_t((mixer[0x2c / 2] & 0x00f0) << 6);
        l.mixshift = uint8_t((mixer[0x2c / 2] >> 8) & 3);
        l.blendmask = 0;
        l.sprblendmask = 0;
        l.coloroffs = compute_color_offsets(mixer, (mixer[0x3e / 2] >> 8) & 1, (mixer[0x3e / 2] >> 14) & 1);
    }
    for (int laynum = 0; laynum < numlayers; laynum++)
        for (int i = laynum + 1; i < numlayers; i++)
            if (layersort[i].effpri > layersort[laynum].effpri) std::swap(layersort[i], layersort[laynum]);

    for (int groupnum = 0; groupnum <= sprgroup_mask; groupnum++) {
        const int effgroup = sprgroup_or | groupnum;
        const int priority = mixer[0x00 / 2 + effgroup] & 0x0f;
        const int effpri = (priority << 3) | 7;
        int sprindex = numlayers, dstnum = 0;
        for (int laynum = 0; laynum < numlayers; laynum++) {
            if (effpri > layersort[laynum].effpri && sprindex == numlayers) sprindex = dstnum++;
            layerorder[groupnum][dstnum++] = layersort[laynum];
        }
        Info &s = layerorder[groupnum][sprindex];
        s.index = LAYER_SPRITES;
        s.effpri = uint8_t(effpri);
        if ((mixer[0x4c / 2] & 3) != 3) s.palbase = uint16_t((mixer[0x00 / 2 + effgroup] & 0x00f0) << 6);
        else s.palbase = uint16_t((mixer[0x4c / 2] & 0x00f0) << 6);
        s.mixshift = uint8_t((mixer[0x00 / 2 + effgroup] >> 8) & 3);
        s.blendmask = 0;
        s.sprblendmask = 0;
        s.coloroffs = compute_color_offsets(mixer, (mixer[0x3e / 2] >> 6) & 1, (mixer[0x4c / 2] >> 15) & 1);
    }

    int sprx_start, sprdx, spry, sprdy;
    if (in_.sprite_latched[0x04 / 2] & 1) sprx_start = cliprect.max_x, sprdx = -1;
    else sprx_start = cliprect.min_x, sprdx = 1;
    if (in_.sprite_latched[0x04 / 2] & 2) spry = cliprect.max_y, sprdy = -1;
    else spry = cliprect.min_y, sprdy = 1;

    const uint16_t *pal = in_.palette;
    for (int y = cliprect.min_y; y <= cliprect.max_y; y++, spry += sprdy) {
        uint32_t *const dest = &frame_[size_t(y) * size_t(width_)];
        const uint16_t *layerbase[8];
        for (int l = LAYER_TEXT; l <= LAYER_BITMAP; ++l) layerbase[l] = layer_scanline(l, y);
        layerbase[LAYER_SPRITES] = layer_scanline(LAYER_SPRITES, spry);
        layerbase[LAYER_BACKGROUND] = layer_scanline(LAYER_BACKGROUND, y);

        for (int x = cliprect.min_x, sprx = sprx_start; x <= cliprect.max_x; x++, sprx += sprdx) {
            const Info *first;
            int laynum, firstpix, shadow = 0;
            const int sprpix = layerbase[LAYER_SPRITES][sprx];
            const int sprgroup = (sprpix >> sprgroup_shift) & sprgroup_mask;
            for (first = &layerorder[sprgroup][0];; first++) {
                laynum = first->index;
                if (laynum != LAYER_SPRITES) {
                    firstpix = layerbase[laynum][x] & 0x1fff;
                    if (firstpix != 0 || laynum == LAYER_BACKGROUND) break;
                } else {
                    firstpix = sprpix;
                    shadow = ~firstpix & sprshadowmask;
                    if ((firstpix & 0x7fff) != 0x7fff) {
                        firstpix &= sprpixmask;
                        if ((firstpix & 0x7ffe) != sprshadow) break;
                        shadow = 1;
                    }
                }
            }
            firstpix = pal[(first->palbase + ((firstpix >> first->mixshift) & 0xfff0) + (firstpix & 0x0f)) & 0x3fff];
            const int *rgbdelta = &rgboffs[first->coloroffs][0];
            int r = ((firstpix >> 0) & 0x1f) + rgbdelta[0];
            int g = ((firstpix >> 5) & 0x1f) + rgbdelta[1];
            int b = ((firstpix >> 10) & 0x1f) + rgbdelta[2];

            if (first->blendmask != 0) {
                const Info *second;
                int secondpix;
                for (second = first + 1;; second++) {
                    laynum = second->index;
                    if (laynum != LAYER_SPRITES) {
                        secondpix = layerbase[laynum][x] & 0x1fff;
                        if (secondpix != 0 || laynum == LAYER_BACKGROUND) break;
                    } else {
                        secondpix = sprpix;
                        shadow = ~secondpix & sprshadowmask;
                        if ((secondpix & 0x7fff) != 0x7fff) {
                            secondpix &= sprpixmask;
                            if ((secondpix & 0x7ffe) != sprshadow) break;
                            shadow = 1;
                        }
                    }
                }
                if (((first->blendmask >> laynum) & 1) &&
                    (laynum != LAYER_SPRITES || ((first->sprblendmask >> sprgroup) & 1))) {
                    secondpix = pal[(second->palbase + ((secondpix >> second->mixshift) & 0xfff0) + (secondpix & 0x0f)) & 0x3fff];
                    r *= 7 - blendfactor;
                    g *= 7 - blendfactor;
                    b *= 7 - blendfactor;
                    rgbdelta = &rgboffs[second->coloroffs][0];
                    r += (((secondpix >> 0) & 0x1f) + rgbdelta[0]) * (blendfactor + 1);
                    g += (((secondpix >> 5) & 0x1f) + rgbdelta[1]) * (blendfactor + 1);
                    b += (((secondpix >> 10) & 0x1f) + rgbdelta[2]) * (blendfactor + 1);
                    r >>= 3;
                    g >>= 3;
                    b >>= 3;
                }
            }
            if (shadow) r >>= 1, g >>= 1, b >>= 1;
            uint32_t out = r > 31 ? 31u << (16 + 3) : r > 0 ? uint32_t(r) << (16 + 3) : 0;
            out |= g > 31 ? 31u << (8 + 3) : g > 0 ? uint32_t(g) << (8 + 3) : 0;
            out |= b > 31 ? 31u << (0 + 3) : b > 0 ? uint32_t(b) << (0 + 3) : 0;
            dest[x] = out;
        }
    }
}

int Video32::render(const Inputs &in) {
    in_ = in;
    width_ = ((in.videoram[0x1ff00 / 2] >> 15) & 1) ? 416 : 320;
    const Rect clip{0, width_ - 1, 0, kHeight - 1};
    if (!in.display_enable) {
        std::fill(frame_.begin(), frame_.begin() + ptrdiff_t(size_t(width_) * kHeight), 0u);
        return width_;
    }
    const uint8_t enablemask = update_tilemaps(clip);
    mix_all_layers(clip, enablemask);
    return width_;
}

} // namespace segas32
