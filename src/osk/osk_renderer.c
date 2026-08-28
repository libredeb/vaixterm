/**
 * @file osk_renderer.c
 * @brief On-Screen Keyboard (OSK) rendering functionality implementation.
 *
 * This module implements all OSK rendering operations including the main
 * rendering coordination, character keys, special keys, and modifier indicators.
 *
 * @author VaixTerm Team
 * @date 2024
 */

#include "osk_renderer.h"
#include "osk_core.h"
#include "error_codes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <SDL_ttf.h>

// Helper function prototypes
static bool is_key_toggled(const Terminal* term, const OnScreenKeyboard* osk, const SpecialKey* key);
static int s_max_mod_indicator_width = -1;

// Padding added around a key's text label.
#define OSK_KEY_PAD_X 8
#define OSK_KEY_PAD_Y 2

// {SHIFT_S} / {SPACE_S} labels (U+21E7 ⇧, U+2423 ␣). Drawn by hand because
// the bundled Martian font does not include these glyphs.
#define OSK_SYM_SHIFT "\xE2\x87\xA7"
#define OSK_SYM_SPACE "\xE2\x90\xA3"

static bool label_is_shift_symbol(const char* label)
{
    return label && strcmp(label, OSK_SYM_SHIFT) == 0;
}

static bool label_is_space_symbol(const char* label)
{
    return label && strcmp(label, OSK_SYM_SPACE) == 0;
}

// Measure the pixel width of a single key label.
static int measure_key_width(TTF_Font* font, const char* label)
{
    if (label_is_shift_symbol(label) || label_is_space_symbol(label)) {
        int w = 0;
        TTF_SizeUTF8(font, "W", &w, NULL);
        if (w < 1) w = 12;
        return w + OSK_KEY_PAD_X * 2;
    }
    int w = 0;
    TTF_SizeUTF8(font, label ? label : "", &w, NULL);
    return w + OSK_KEY_PAD_X * 2;
}

static int max_int(int a, int b) { return a > b ? a : b; }
static int min_int(int a, int b) { return a < b ? a : b; }

static void draw_shift_symbol(SDL_Renderer* renderer, SDL_Rect key_rect)
{
    int pad = max_int(2, min_int(key_rect.w, key_rect.h) / 6);
    int x = key_rect.x + pad;
    int y = key_rect.y + pad;
    int w = key_rect.w - pad * 2;
    int h = key_rect.h - pad * 2;
    if (w < 4 || h < 4) return;

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    int mid_x = x + w / 2;
    int head_h = max_int(3, (h * 55) / 100);
    for (int row = 0; row < head_h; row++) {
        int half = (row * (w / 2)) / head_h;
        SDL_Rect line = {mid_x - half, y + row, half * 2 + 1, 1};
        SDL_RenderFillRect(renderer, &line);
    }
    int stem_w = max_int(2, w / 4);
    int stem_x = mid_x - stem_w / 2;
    int stem_y = y + (head_h * 70) / 100;
    SDL_Rect stem = {stem_x, stem_y, stem_w, y + h - stem_y};
    if (stem.h > 0) SDL_RenderFillRect(renderer, &stem);
}

static void draw_space_symbol(SDL_Renderer* renderer, SDL_Rect key_rect)
{
    int pad = max_int(2, min_int(key_rect.w, key_rect.h) / 5);
    int x = key_rect.x + pad;
    int y = key_rect.y + pad + key_rect.h / 8;
    int w = key_rect.w - pad * 2;
    int h = key_rect.h - pad * 2 - key_rect.h / 8;
    if (w < 4 || h < 4) return;

    int t = max_int(2, min_int(w, h) / 8);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_Rect left = {x, y, t, h};
    SDL_Rect bottom = {x, y + h - t, w, t};
    SDL_Rect right = {x + w - t, y, t, h};
    SDL_RenderFillRect(renderer, &left);
    SDL_RenderFillRect(renderer, &bottom);
    SDL_RenderFillRect(renderer, &right);
}

// Draw a single key (background, border, label) at the given x.
// The box keeps its size; the label is scaled down (never stretched up)
// so long words like "Shift" stay fully readable inside the cell.
static void draw_key(SDL_Renderer* renderer, TTF_Font* font,
                     const char* label, int x, int osk_y, int w, int char_h,
                     bool selected, bool toggled, SDL_Color normal_color)
{
    SDL_Rect key_rect = {x, osk_y, w, char_h};

    if (selected) {
        SDL_SetRenderDrawColor(renderer, 220, 220, 50, 255); // Yellow for selected cursor
    } else if (toggled) {
        SDL_SetRenderDrawColor(renderer, 100, 150, 255, 255); // Blue for toggled
    } else {
        SDL_SetRenderDrawColor(renderer, normal_color.r, normal_color.g, normal_color.b, 255);
    }
    SDL_RenderFillRect(renderer, &key_rect);

    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
    SDL_RenderDrawRect(renderer, &key_rect);

    if (!label) return;

    SDL_Rect prev_clip;
    SDL_RenderGetClipRect(renderer, &prev_clip);
    SDL_bool was_clipping = SDL_RenderIsClipEnabled(renderer);
    SDL_RenderSetClipRect(renderer, &key_rect);

    if (label_is_shift_symbol(label)) {
        draw_shift_symbol(renderer, key_rect);
    } else if (label_is_space_symbol(label)) {
        draw_space_symbol(renderer, key_rect);
    } else {
        SDL_Color text_color = {255, 255, 255, 255};
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, label, text_color);
        if (surface) {
            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
            if (texture) {
                int text_w = surface->w;
                int text_h = surface->h;
                int inner_w = max_int(1, w - 4);
                int inner_h = max_int(1, char_h - OSK_KEY_PAD_Y * 2);
                int dst_w = text_w;
                int dst_h = text_h;
                if (text_w > inner_w || text_h > inner_h) {
                    float sx = (float)inner_w / (float)max_int(1, text_w);
                    float sy = (float)inner_h / (float)max_int(1, text_h);
                    float scale = sx < sy ? sx : sy;
                    if (scale < 1.0f) {
                        dst_w = max_int(1, (int)(text_w * scale));
                        dst_h = max_int(1, (int)(text_h * scale));
                    }
                }
                SDL_Rect text_rect = {
                    x + (w - dst_w) / 2,
                    osk_y + (char_h - dst_h) / 2,
                    dst_w,
                    dst_h
                };
                SDL_RenderCopy(renderer, texture, NULL, &text_rect);
                SDL_DestroyTexture(texture);
            }
            SDL_FreeSurface(surface);
        }
    }

    if (was_clipping) {
        SDL_RenderSetClipRect(renderer, &prev_clip);
    } else {
        SDL_RenderSetClipRect(renderer, NULL);
    }
}

// Size each key in a row from its label, then grow (or shrink) the set so
// the row fills avail_w. Wider labels such as "Shift" keep a wider box.
static void layout_row_widths(TTF_Font* font, const SpecialKey* keys, int count,
                               int avail_w, int* widths)
{
    if (count <= 0 || !widths) return;

    int total = 0;
    for (int i = 0; i < count; i++) {
        widths[i] = measure_key_width(font, keys[i].display_name);
        if (widths[i] < 1) widths[i] = 1;
        total += widths[i];
    }
    if (total < 1) total = 1;

    if (total == avail_w) return;

    if (total > avail_w) {
        int used = 0;
        for (int i = 0; i < count; i++) {
            widths[i] = max_int(1, (int)(((long)widths[i] * avail_w) / total));
            used += widths[i];
        }
        widths[count - 1] += avail_w - used;
        if (widths[count - 1] < 1) widths[count - 1] = 1;
        return;
    }

    int extra = avail_w - total;
    int add = extra / count;
    int rem = extra % count;
    for (int i = 0; i < count; i++) {
        widths[i] += add + (i < rem ? 1 : 0);
    }
}

// Render a single centered line of keys where the selected key is always
// centered within [avail_left, avail_right) and the rest lay out around it.
static int render_osk_line(SDL_Renderer* renderer, TTF_Font* font,
                           const SpecialKey* keys, int count, int selected_idx,
                           const OnScreenKeyboard* osk, const Terminal* term,
                           int avail_left, int avail_right, int osk_y, int char_h)
{
    if (count <= 0 || selected_idx < 0 || selected_idx >= count) return 0;
    if (avail_right <= avail_left) return 0;

    // Measure every key width and find the selected key's center.
    int* widths = calloc((size_t)count, sizeof(int));
    if (!widths) return 0;

    for (int i = 0; i < count; i++) {
        widths[i] = measure_key_width(font, keys[i].display_name);
    }

    int band_center = (avail_left + avail_right) / 2;
    int offset_before = 0;
    for (int i = 0; i < selected_idx; i++) offset_before += widths[i];
    int selected_x = band_center - (offset_before + widths[selected_idx] / 2);

    // Clip to the OSK bar and render only the keys that fall inside the band.
    SDL_Rect clip = {0, osk_y, avail_right, char_h};
    SDL_RenderSetClipRect(renderer, &clip);

    int drawn = 0;
    int x = selected_x;
    bool has_left_overflow = false;
    bool has_right_overflow = false;
    for (int i = 0; i < count; i++) {
        int key_end = x + widths[i];
        if (key_end > avail_left && x < avail_right) {
            bool toggled = is_key_toggled(term, osk, &keys[i]);
            draw_key(renderer, font, keys[i].display_name, x, osk_y, widths[i], char_h,
                     i == selected_idx, toggled, (SDL_Color){60, 60, 60, 255});
            drawn++;
        }
        if (x < avail_left) has_left_overflow = true;
        if (key_end > avail_right) has_right_overflow = true;
        x += widths[i];
    }

    // Draw scroll arrows if the row extends beyond the visible band.
    if (has_left_overflow || has_right_overflow) {
        SDL_Color arrow_color = {160, 160, 160, 255};
        const char* arrow_left = has_left_overflow ? "<" : NULL;
        const char* arrow_right = has_right_overflow ? ">" : NULL;
        if (arrow_left) {
            int aw = 0;
            TTF_SizeUTF8(font, arrow_left, &aw, NULL);
            SDL_Surface* surf = TTF_RenderUTF8_Blended(font, arrow_left, arrow_color);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
                if (tex) {
                    SDL_Rect dst = {avail_left - aw - 2, osk_y + (char_h - surf->h) / 2, surf->w, surf->h};
                    SDL_RenderCopy(renderer, tex, NULL, &dst);
                    SDL_DestroyTexture(tex);
                }
                SDL_FreeSurface(surf);
            }
        }
        if (arrow_right) {
            int aw = 0;
            TTF_SizeUTF8(font, arrow_right, &aw, NULL);
            SDL_Surface* surf = TTF_RenderUTF8_Blended(font, arrow_right, arrow_color);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
                if (tex) {
                    SDL_Rect dst = {avail_right + 2, osk_y + (char_h - surf->h) / 2, surf->w, surf->h};
                    SDL_RenderCopy(renderer, tex, NULL, &dst);
                    SDL_DestroyTexture(tex);
                }
                SDL_FreeSurface(surf);
            }
        }
    }

    SDL_RenderSetClipRect(renderer, NULL);
    free(widths);
    return drawn;
}

static void osk_build_left_label(const OnScreenKeyboard* osk, char* left_label, size_t size)
{
    if (osk->mode == OSK_MODE_SPECIAL) {
        const SpecialKeySet* active_set = get_active_special_set(osk);
        const char* name = (active_set && active_set->name) ? active_set->name : "BASE";
        if (osk->num_total_special_sets > 1) {
            snprintf(left_label, size, "%s [%d/%d]",
                     name, osk->set_idx + 1, osk->num_total_special_sets);
        } else {
            snprintf(left_label, size, "%s", name);
        }
    } else {
        snprintf(left_label, size, "R%d/%d",
                 osk->current_char_row + 1, get_current_num_char_rows(osk));
    }
}

static void osk_draw_left_label(SDL_Renderer* renderer, TTF_Font* font,
                               const char* left_label, int osk_y, int bar_h)
{
    if (!left_label || left_label[0] == '\0') return;
    SDL_Color text_color = {200, 200, 200, 255};
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, left_label, text_color);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect text_rect = {
            OSK_KEY_PAD_X / 2,
            osk_y + (bar_h - surface->h) / 2,
            surface->w,
            surface->h
        };
        SDL_RenderCopy(renderer, texture, NULL, &text_rect);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

static int special_set_min_cell_w(TTF_Font* font, const SpecialKeySet* set)
{
    int max_w = 0;
    if (!set || !set->keys) return 24;
    for (int i = 0; i < set->num_keys; i++) {
        int w = measure_key_width(font, set->keys[i].display_name);
        if (w > max_w) max_w = w;
    }
    if (max_w < 24) max_w = 24;
    return max_w;
}

static void render_osk_grid(SDL_Renderer* renderer, TTF_Font* font, OnScreenKeyboard* osk,
                            const Terminal* term, int win_w, int win_h, int char_w, int char_h,
                            const Config* config)
{
    int osk_alpha = config ? config->osk_alpha : 220;
    int font_h = TTF_FontHeight(font);
    int min_cell_h = font_h + OSK_KEY_PAD_Y * 2;
    int cell_h;
    if (config && config->osk_bar_height > 0) {
        cell_h = config->osk_bar_height;
    } else {
        cell_h = char_h > min_cell_h ? char_h : min_cell_h;
    }
    const int pad = 4;

    char left_label[64] = {0};
    osk_build_left_label(osk, left_label, sizeof(left_label));

    int avail_left = pad;
    int avail_right = win_w - pad;
    int avail_w = avail_right - avail_left;
    if (avail_w < 1) avail_w = 1;

    int grid_rows = 1;
    int special_cols = 1;
    const SpecialKeySet* special_set = NULL;

    if (osk->mode == OSK_MODE_SPECIAL) {
        special_set = get_active_special_set(osk);
        int n = (special_set && special_set->keys) ? special_set->num_keys : 0;
        int min_cell = special_set_min_cell_w(font, special_set);
        special_cols = osk_special_grid_column_count(n, avail_w, min_cell);
        osk->grid_cols = special_cols;
        if (n > 0) {
            grid_rows = (n + special_cols - 1) / special_cols;
        }
        if (grid_rows < 1) grid_rows = 1;
    } else {
        grid_rows = get_current_num_char_rows(osk);
        if (grid_rows < 1) grid_rows = 1;
    }

    int header_h = cell_h;
    int total_h = header_h + grid_rows * cell_h;
    if (total_h > win_h && grid_rows > 0) {
        int remain = win_h - header_h;
        if (remain < 8) remain = 8;
        cell_h = remain / grid_rows;
        if (cell_h < 8) cell_h = 8;
        total_h = header_h + grid_rows * cell_h;
        if (total_h > win_h) total_h = win_h;
    }

    int osk_y = get_osk_y_position(osk, term, win_h, total_h);

    SDL_Rect bg_rect = {0, osk_y, win_w, total_h};
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, osk_alpha);
    SDL_RenderFillRect(renderer, &bg_rect);

    osk_draw_left_label(renderer, font, left_label, osk_y, header_h);
    render_modifier_indicators(renderer, font, osk, win_w, osk_y, header_h, char_w);

    int keys_y = osk_y + header_h;
    SDL_Color key_bg = {60, 60, 60, 255};

    if (osk->mode == OSK_MODE_SPECIAL) {
        int n = (special_set && special_set->keys) ? special_set->num_keys : 0;
        if (n > 0 && special_cols > 0) {
            int cell_w = avail_w / special_cols;
            if (cell_w < 1) cell_w = 1;
            for (int i = 0; i < n; i++) {
                int r = i / special_cols;
                int c = i % special_cols;
                int x = avail_left + c * cell_w;
                int y = keys_y + r * cell_h;
                bool toggled = is_key_toggled(term, osk, &special_set->keys[i]);
                draw_key(renderer, font, special_set->keys[i].display_name,
                         x, y, cell_w, cell_h,
                         i == osk->char_idx, toggled, key_bg);
            }
        }
    } else {
        int num_rows = get_current_num_char_rows(osk);
        for (int r = 0; r < num_rows; r++) {
            const SpecialKeySet* row = osk_get_effective_row_ptr(osk, r);
            if (!row || !row->keys || row->num_keys <= 0) continue;
            int* widths = calloc((size_t)row->num_keys, sizeof(int));
            if (!widths) continue;
            layout_row_widths(font, row->keys, row->num_keys, avail_w, widths);
            int y = keys_y + r * cell_h;
            int x = avail_left;
            for (int i = 0; i < row->num_keys; i++) {
                bool selected = (r == osk->current_char_row && i == osk->char_idx);
                bool toggled = is_key_toggled(term, osk, &row->keys[i]);
                draw_key(renderer, font, row->keys[i].display_name,
                         x, y, widths[i], cell_h,
                         selected, toggled, key_bg);
                x += widths[i];
            }
            free(widths);
        }
    }
}

void render_osk(SDL_Renderer* renderer, TTF_Font* font, OnScreenKeyboard* osk, const Terminal* term, 
                int win_w, int win_h, int char_w, int char_h, const Config* config)
{
    if (!renderer || !font || !osk || !term || win_w <= 0 || win_h <= 0 || char_h <= 0) {
        ERROR_LOG("Invalid parameters: renderer=%p, font=%p, osk=%p, term=%p, win_w=%d, win_h=%d, char_h=%d",
                  (void*)renderer, (void*)font, (void*)osk, (void*)term, win_w, win_h, char_h);
        return;
    }

    osk->grid_mode = config && config->osk_grid;
    if (osk->grid_mode) {
        render_osk_grid(renderer, font, osk, term, win_w, win_h, char_w, char_h, config);
        return;
    }

    int osk_alpha = config ? config->osk_alpha : 220;
    int font_h = TTF_FontHeight(font);
    int min_cell_h = font_h + OSK_KEY_PAD_Y * 2;
    int bar_h;
    if (config && config->osk_bar_height > 0) {
        bar_h = config->osk_bar_height;
    } else {
        bar_h = char_h > min_cell_h ? char_h : min_cell_h;
    }

    // Single centered line, anchored top or bottom per position_mode.
    int osk_y = get_osk_y_position(osk, term, win_h, bar_h);

    SDL_Rect bg_rect = {0, osk_y, win_w, bar_h};
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, osk_alpha);
    SDL_RenderFillRect(renderer, &bg_rect);

    // Reserve a right gutter for the ^ASG modifier indicators and a left
    // gutter for the current set/row label, so the centered key line never
    // hides underneath either.
    int indicator_w = get_modifier_indicators_width(font, char_w);
    int left_gutter = 0;
    char left_label[64] = {0};
    osk_build_left_label(osk, left_label, sizeof(left_label));
    int label_w = 0;
    TTF_SizeUTF8(font, left_label, &label_w, NULL);
    left_gutter = label_w + OSK_KEY_PAD_X;

    DEBUG_LOG("Rendering OSK: mode=%s set_idx=%d num_special_sets=%d label='%s'",
              osk->mode == OSK_MODE_SPECIAL ? "SPECIAL" : "CHARS",
              osk->set_idx, osk->num_total_special_sets, left_label);

    int avail_left = left_gutter;
    int avail_right = win_w - indicator_w;

    osk_draw_left_label(renderer, font, left_label, osk_y, bar_h);

    if (osk->mode == OSK_MODE_SPECIAL) {
        const SpecialKeySet* active_set = get_active_special_set(osk);
        if (active_set && active_set->keys && active_set->num_keys > 0) {
            render_osk_line(renderer, font, active_set->keys, active_set->num_keys,
                            osk->char_idx, osk, term, avail_left, avail_right, osk_y, bar_h);
        }
    } else {
        int effective_num_rows = get_current_num_char_rows(osk);
        if (effective_num_rows > 0) {
            const SpecialKeySet* row = osk_get_effective_row_ptr(osk, osk->current_char_row);
            if (row && row->keys && row->num_keys > 0) {
                render_osk_line(renderer, font, row->keys, row->num_keys,
                                osk->char_idx, osk, term, avail_left, avail_right, osk_y, bar_h);
            }
        }
    }

    render_modifier_indicators(renderer, font, osk, win_w, osk_y, bar_h, char_w);
}

void render_key_tape(SDL_Renderer* renderer, TTF_Font* font, OnScreenKeyboard* osk,
                      int win_w, int char_w, int char_h)
{
    (void)renderer; (void)font; (void)osk; (void)win_w; (void)char_w; (void)char_h;
    DEBUG_LOG("Rendering key tape (simplified implementation)");
}

void render_osk_chars(SDL_Renderer* renderer, TTF_Font* font, OnScreenKeyboard* osk,
                       int available_width, int osk_y, int char_w, int char_h)
{
    (void)renderer; (void)font; (void)osk; (void)available_width; (void)osk_y;
    (void)char_w; (void)char_h;
    DEBUG_LOG("render_osk_chars is superseded by render_osk() centered layout");
}

void render_osk_special(SDL_Renderer* renderer, TTF_Font* font, OnScreenKeyboard* osk,
                        const Terminal* term, int available_width, int osk_y, 
                        int char_w, int char_h)
{
    (void)renderer; (void)font; (void)osk; (void)term; (void)available_width;
    (void)osk_y; (void)char_w; (void)char_h;
    DEBUG_LOG("render_osk_special is superseded by render_osk() centered layout");
}

void render_modifier_indicators(SDL_Renderer* renderer, TTF_Font* font, OnScreenKeyboard* osk,
                                 int win_w, int osk_y, int char_h, int char_w)
{
    if (!renderer || !font || !osk || win_w <= 0 || char_h <= 0) {
        ERROR_LOG("Invalid parameters: renderer=%p, font=%p, osk=%p, win_w=%d, char_h=%d",
                  (void*)renderer, (void*)font, (void*)osk, win_w, char_h);
        return;
    }

    // Minimal modifier indicators: one glyph per active modifier.
    //   ^ = Ctrl, A = Alt, S = Shift, G = GUI
    // Shows both the one-shot (toggled) and physically held controller modifiers.
    const char* glyphs[] = {"^", "A", "S", "G"};
    bool active[] = {
        osk->mod_ctrl  || osk->held_ctrl,
        osk->mod_alt   || osk->held_alt,
        osk->mod_shift || osk->held_shift,
        osk->mod_gui   || osk->held_gui
    };
    int num = sizeof(glyphs) / sizeof(glyphs[0]);

    int cell_w = char_w + 2; // compact square-ish cell

    // Right-aligned cluster of active glyphs.
    int x = win_w;
    for (int i = num - 1; i >= 0; i--) {
        if (!active[i]) continue;
        x -= cell_w;

        SDL_SetRenderDrawColor(renderer, 90, 90, 110, 255);
        SDL_Rect indicator_rect = {x, osk_y, cell_w, char_h};
        SDL_RenderFillRect(renderer, &indicator_rect);

        SDL_Color text_color = {255, 255, 255, 255};
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, glyphs[i], text_color);
        if (surface) {
            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
            if (texture) {
                SDL_Rect text_rect = {
                    x + (cell_w - surface->w) / 2,
                    osk_y + (char_h - surface->h) / 2,
                    surface->w,
                    surface->h
                };
                SDL_RenderCopy(renderer, texture, NULL, &text_rect);
                SDL_DestroyTexture(texture);
            }
            SDL_FreeSurface(surface);
        }
    }

    DEBUG_LOG("Rendered modifier indicators");
}

int get_modifier_indicators_width(TTF_Font* font, int char_w)
{
    if (!font || char_w <= 0) {
        ERROR_LOG("Invalid parameters: font=%p, char_w=%d", (void*)font, char_w);
        return 0;
    }

    // Cache the width calculation
    if (s_max_mod_indicator_width >= 0) {
        return s_max_mod_indicator_width;
    }

    int num_indicators = 4; // ^ A S G
    int cell_w = char_w + 2;
    s_max_mod_indicator_width = num_indicators * cell_w;

    DEBUG_LOG("Calculated modifier indicators width: %d", s_max_mod_indicator_width);
    return s_max_mod_indicator_width;
}

void osk_invalidate_render_cache(OnScreenKeyboard* osk)
{
    if (!osk) {
        ERROR_LOG("Invalid parameter: osk=%p", (void*)osk);
        return;
    }

    osk->cached_set_idx = -1;
    osk->cached_mod_mask = -1;
    osk->cached_key_width = -1;
    s_max_mod_indicator_width = -1;
    DEBUG_LOG("Invalidated OSK render cache");
}

// Helper function implementations
static bool is_key_toggled(const Terminal* term, const OnScreenKeyboard* osk, const SpecialKey* key) {
    (void)term;
    (void)osk;
    if (!key || !osk) return false;

    switch (key->type) {
        case SK_MOD_CTRL:
            return osk->mod_ctrl;
        case SK_MOD_ALT:
            return osk->mod_alt;
        case SK_MOD_GUI:
            return osk->mod_gui;
        case SK_MOD_SHIFT:
            return osk->mod_shift;
        default:
            return false;
    }
}

OSKKeyCache* osk_key_cache_create(void)
{
    OSKKeyCache* cache = calloc(1, sizeof(OSKKeyCache));
    if (!cache) {
        ERROR_LOG("Failed to allocate OSK key cache");
        return NULL;
    }
    
    DEBUG_LOG("Created OSK key cache with %d entries", OSK_KEY_CACHE_SIZE);
    return cache;
}

void osk_key_cache_destroy(OSKKeyCache* cache)
{
    if (!cache) {
        return;
    }
    
    int destroyed_count = 0;
    for (int i = 0; i < OSK_KEY_CACHE_SIZE; ++i) {
        if (cache->entries[i].texture) {
            SDL_DestroyTexture(cache->entries[i].texture);
            cache->entries[i].texture = NULL;
            destroyed_count++;
        }
    }
    
    DEBUG_LOG("Destroyed OSK key cache, freed %d textures", destroyed_count);
    free(cache);
}
