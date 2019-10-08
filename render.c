#include <math.h>
#include <stdlib.h>
#include <wayland-client.h>
#include "cairo.h"
#include "background-image.h"
#include "swaylock.h"

#define M_PI 3.14159265358979323846
const float TYPE_INDICATOR_RANGE = M_PI / 3.0f;
const float TYPE_INDICATOR_BORDER_THICKNESS = M_PI / 128.0f;

static void set_color_for_state(cairo_t *cairo, struct swaylock_state *state,
		struct swaylock_colorset *colorset) {
	if (state->auth_state == AUTH_STATE_VALIDATING) {
		cairo_set_source_u32(cairo, colorset->verifying);
	} else if (state->auth_state == AUTH_STATE_INVALID) {
		cairo_set_source_u32(cairo, colorset->wrong);
	} else if (state->auth_state == AUTH_STATE_CLEAR) {
		cairo_set_source_u32(cairo, colorset->cleared);
	} else {
		if (state->xkb.caps_lock && state->args.show_caps_lock_indicator) {
			cairo_set_source_u32(cairo, colorset->caps_lock);
		} else if (state->xkb.caps_lock && !state->args.show_caps_lock_indicator &&
				state->args.show_caps_lock_text) {
			uint32_t inputtextcolor = state->args.colors.text.input;
			state->args.colors.text.input = state->args.colors.text.caps_lock;
			cairo_set_source_u32(cairo, colorset->input);
			state->args.colors.text.input = inputtextcolor;
		} else {
			cairo_set_source_u32(cairo, colorset->input);
		}
	}
}

void render_frame_background(struct swaylock_surface *surface) {
	struct swaylock_state *state = surface->state;

	int buffer_width = surface->width * surface->scale;
	int buffer_height = surface->height * surface->scale;
	if (buffer_width == 0 || buffer_height == 0) {
		return; // not yet configured
	}

	surface->current_buffer = get_next_buffer(state->shm,
			surface->buffers, buffer_width, buffer_height);
	if (surface->current_buffer == NULL) {
		return;
	}

	cairo_t *cairo = surface->current_buffer->cairo;
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);

	cairo_save(cairo);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_u32(cairo, state->args.colors.background);
	cairo_paint(cairo);
	if (surface->image && state->args.mode != BACKGROUND_MODE_SOLID_COLOR) {
		cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);
		render_background_image(cairo, surface->image,
			state->args.mode, buffer_width, buffer_height);
	}
	cairo_restore(cairo);
	cairo_identity_matrix(cairo);

	wl_surface_set_buffer_scale(surface->surface, surface->scale);
	wl_surface_attach(surface->surface, surface->current_buffer->buffer, 0, 0);
	wl_surface_damage(surface->surface, 0, 0, surface->width, surface->height);
	wl_surface_commit(surface->surface);
}

struct Point {
	int x;
	int y;
	struct wl_list link;
};

void make_regular_polygon(struct wl_list* coords, size_t vertices, int radius, int center_x, int center_y) {
	/*
	struct Point *p1 = (struct Point*) malloc(sizeof(struct Point));
	struct Point *p2 = (struct Point*) malloc(sizeof(struct Point));
	struct Point *p3 = (struct Point*) malloc(sizeof(struct Point));

	p1->x = 1;
	p2->x = 2;
	p3->x = 3;

	wl_list_insert(coords, &p1->link);
	wl_list_insert(coords, &p2->link);
	wl_list_insert(coords, &p3->link);
	*/

  // vertices + 1 to close the shape
  for (size_t i = 0; i < vertices + 1; i++) {
    int x = round(radius * sin((i+1) * 2 * M_PI / vertices));
    int y = round(radius * cos((i+1) * 2 * M_PI / vertices));
		struct Point *point = (struct Point*) malloc(sizeof(struct Point));
		point->x = center_x + x;
		point->y = center_y - y;

		wl_list_insert(coords, &point->link);
  }
}

void draw_point_list(cairo_t* cairo, struct wl_list* coords) {
	struct Point *p;
	wl_list_for_each(p, coords, link) {
		printf("%d\n", p->x);
		cairo_line_to(cairo, p->x, p->y);
	}
	cairo_stroke(cairo);
}

void render_frame(struct swaylock_surface *surface) {
	struct swaylock_state *state = surface->state;

	int arc_radius = state->args.radius * surface->scale;
	int arc_thickness = state->args.thickness * surface->scale;
	int buffer_diameter = (arc_radius + arc_thickness) * 2;

	int buffer_width = surface->indicator_width;
	int buffer_height = surface->indicator_height;
	int new_width = buffer_diameter;
	int new_height = buffer_diameter;

	int subsurf_xpos = surface->width / 2 -
		buffer_width / (2 * surface->scale) + 2 / surface->scale;
	int subsurf_ypos = surface->height / 2 -
		(state->args.radius + state->args.thickness);
	wl_subsurface_set_position(surface->subsurface, subsurf_xpos, subsurf_ypos);

	surface->current_buffer = get_next_buffer(state->shm,
			surface->indicator_buffers, buffer_width, buffer_height);
	if (surface->current_buffer == NULL) {
		return;
	}

	// Hide subsurface until we want it visible
	wl_surface_attach(surface->child, NULL, 0, 0);
	wl_surface_commit(surface->child);

	cairo_t *cairo = surface->current_buffer->cairo;
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);
	cairo_font_options_t *fo = cairo_font_options_create();
	cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_FULL);
	cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_SUBPIXEL);
	cairo_font_options_set_subpixel_order(fo, to_cairo_subpixel_order(surface->subpixel));
	cairo_set_font_options(cairo, fo);
	cairo_font_options_destroy(fo);
	cairo_identity_matrix(cairo);

	// Clear
	cairo_save(cairo);
	cairo_set_source_rgba(cairo, 0, 0, 0, 0);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cairo);
	cairo_restore(cairo);

	float type_indicator_border_thickness =
		TYPE_INDICATOR_BORDER_THICKNESS * surface->scale;

	if (state->args.show_fractal) {
		if (state->args.show_indicator && (state->auth_state != AUTH_STATE_IDLE ||
				state->args.indicator_idle_visible)) {

				struct wl_list fractal_list;
				wl_list_init(&fractal_list);

				make_regular_polygon(&fractal_list, 3, arc_radius, buffer_width / 2, buffer_diameter / 2);

				draw_point_list(cairo, &fractal_list);

				struct Point *p;
				wl_list_for_each(p, &fractal_list, link) {
					free(p);
				}
				/*
				struct Point *last = NULL;

				wl_list_for_each(pos, &fractal_list, link) {
					cairo_move_to(cairo, pos->x, pos->y);
					if(last != NULL) {
						cairo_move_to(cairo, last->x, last->y);
						cairo_line_to(cairo, p->x, p->y);
						cairo_stroke(cairo);
					}
					last = pos;
				}*/
			/*
			// Draw circle

			cairo_set_line_width(cairo, arc_thickness);
			cairo_arc(cairo, buffer_width / 2, buffer_diameter / 2, arc_radius,
					0, 2 * M_PI);
			set_color_for_state(cairo, state, &state->args.colors.inside);
			cairo_fill_preserve(cairo);
			set_color_for_state(cairo, state, &state->args.colors.ring);
			cairo_stroke(cairo);

			// Draw a message
			char *text = NULL;
			const char *layout_text = NULL;
			char attempts[4]; // like i3lock: count no more than 999
			set_color_for_state(cairo, state, &state->args.colors.text);
			cairo_select_font_face(cairo, state->args.font,
					CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
			if (state->args.font_size > 0) {
				cairo_set_font_size(cairo, state->args.font_size);
			} else {
				cairo_set_font_size(cairo, arc_radius / 3.0f);
			}
			switch (state->auth_state) {
			case AUTH_STATE_VALIDATING:
				text = "verifying";
				break;
			case AUTH_STATE_INVALID:
				text = "wrong";
				break;
			case AUTH_STATE_CLEAR:
				text = "cleared";
				break;
			case AUTH_STATE_INPUT:
			case AUTH_STATE_INPUT_NOP:
			case AUTH_STATE_BACKSPACE:
				// Caps Lock has higher priority
				if (state->xkb.caps_lock && state->args.show_caps_lock_text) {
					text = "Caps Lock";
				} else if (state->args.show_failed_attempts &&
						state->failed_attempts > 0) {
					if (state->failed_attempts > 999) {
						text = "999+";
					} else {
						snprintf(attempts, sizeof(attempts), "%d", state->failed_attempts);
						text = attempts;
					}
				}

				xkb_layout_index_t num_layout = xkb_keymap_num_layouts(state->xkb.keymap);
				if (!state->args.hide_keyboard_layout &&
						(state->args.show_keyboard_layout || num_layout > 1)) {
					xkb_layout_index_t curr_layout = 0;

					// advance to the first active layout (if any)
					while (curr_layout < num_layout &&
						xkb_state_layout_index_is_active(state->xkb.state,
							curr_layout, XKB_STATE_LAYOUT_EFFECTIVE) != 1) {
						++curr_layout;
					}
					// will handle invalid index if none are active
					layout_text = xkb_keymap_layout_get_name(state->xkb.keymap, curr_layout);
				}
				break;
			default:
				break;
			}

			if (text) {
				cairo_text_extents_t extents;
				cairo_font_extents_t fe;
				double x, y;
				cairo_text_extents(cairo, text, &extents);
				cairo_font_extents(cairo, &fe);
				x = (buffer_width / 2) -
					(extents.width / 2 + extents.x_bearing);
				y = (buffer_diameter / 2) +
					(fe.height / 2 - fe.descent);

				cairo_move_to(cairo, x, y);
				cairo_show_text(cairo, text);
				cairo_close_path(cairo);
				cairo_new_sub_path(cairo);

				if (new_width < extents.width) {
					new_width = extents.width;
				}
			}

			// Typing indicator: Highlight random part on keypress

			if (state->auth_state == AUTH_STATE_INPUT
					|| state->auth_state == AUTH_STATE_BACKSPACE) {
				static double highlight_start = 0;
				highlight_start +=
					(rand() % (int)(M_PI * 100)) / 100.0 + M_PI * 0.5;
				cairo_arc(cairo, buffer_width / 2, buffer_diameter / 2,
						arc_radius, highlight_start,
						highlight_start + TYPE_INDICATOR_RANGE);
				if (state->auth_state == AUTH_STATE_INPUT) {
					if (state->xkb.caps_lock && state->args.show_caps_lock_indicator) {
						cairo_set_source_u32(cairo, state->args.colors.caps_lock_key_highlight);
					} else {
						cairo_set_source_u32(cairo, state->args.colors.key_highlight);
					}
				} else {
					if (state->xkb.caps_lock && state->args.show_caps_lock_indicator) {
						cairo_set_source_u32(cairo, state->args.colors.caps_lock_bs_highlight);
					} else {
						cairo_set_source_u32(cairo, state->args.colors.bs_highlight);
					}
				}
				cairo_stroke(cairo);

				// Draw borders

				cairo_set_source_u32(cairo, state->args.colors.separator);
				cairo_arc(cairo, buffer_width / 2, buffer_diameter / 2,
						arc_radius, highlight_start,
						highlight_start + type_indicator_border_thickness);
				cairo_stroke(cairo);

				cairo_arc(cairo, buffer_width / 2, buffer_diameter / 2,
						arc_radius, highlight_start + TYPE_INDICATOR_RANGE,
						highlight_start + TYPE_INDICATOR_RANGE +
							type_indicator_border_thickness);
				cairo_stroke(cairo);
			}

			// Draw inner + outer border of the circle

			set_color_for_state(cairo, state, &state->args.colors.line);
			cairo_set_line_width(cairo, 2.0 * surface->scale);
			cairo_arc(cairo, buffer_width / 2, buffer_diameter / 2,
					arc_radius - arc_thickness / 2, 0, 2 * M_PI);
			cairo_stroke(cairo);
			cairo_arc(cairo, buffer_width / 2, buffer_diameter / 2,
					arc_radius + arc_thickness / 2, 0, 2 * M_PI);
			cairo_stroke(cairo);

			// display layout text seperately
			if (layout_text) {
				cairo_text_extents_t extents;
				cairo_font_extents_t fe;
				double x, y;
				double box_padding = 4.0 * surface->scale;
				cairo_text_extents(cairo, layout_text, &extents);
				cairo_font_extents(cairo, &fe);
				// upper left coordinates for box
				x = (buffer_width / 2) - (extents.width / 2) - box_padding;
				y = buffer_diameter;

				// background box
				cairo_rectangle(cairo, x, y,
					extents.width + 2.0 * box_padding,
					fe.height + 2.0 * box_padding);
				cairo_set_source_u32(cairo, state->args.colors.layout_background);
				cairo_fill_preserve(cairo);
				// border
				cairo_set_source_u32(cairo, state->args.colors.layout_border);
				cairo_stroke(cairo);

				// take font extents and padding into account
				cairo_move_to(cairo,
					x - extents.x_bearing + box_padding,
					y + (fe.height - fe.descent) + box_padding);
				cairo_set_source_u32(cairo, state->args.colors.layout_text);
				cairo_show_text(cairo, layout_text);
				cairo_new_sub_path(cairo);

				new_height += fe.height + 2 * box_padding;
				if (new_width < extents.width + 2 * box_padding) {
					new_width = extents.width + 2 * box_padding;
				}
			}*/

			if (buffer_width != new_width || buffer_height != new_height) {
				destroy_buffer(surface->current_buffer);
				surface->indicator_width = new_width;
				surface->indicator_height = new_height;
				render_frame(surface);
			}
		}
	}
	else {
		if (state->args.show_indicator && (state->auth_state != AUTH_STATE_IDLE ||
				state->args.indicator_idle_visible)) {
			// Draw circle
			cairo_set_line_width(cairo, arc_thickness);
			cairo_arc(cairo, buffer_width / 2, buffer_diameter / 2, arc_radius,
					0, 2 * M_PI);
			set_color_for_state(cairo, state, &state->args.colors.inside);
			cairo_fill_preserve(cairo);
			set_color_for_state(cairo, state, &state->args.colors.ring);
			cairo_stroke(cairo);

			// Draw a message
			char *text = NULL;
			const char *layout_text = NULL;
			char attempts[4]; // like i3lock: count no more than 999
			set_color_for_state(cairo, state, &state->args.colors.text);
			cairo_select_font_face(cairo, state->args.font,
					CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
			if (state->args.font_size > 0) {
				cairo_set_font_size(cairo, state->args.font_size);
			} else {
				cairo_set_font_size(cairo, arc_radius / 3.0f);
			}
			switch (state->auth_state) {
			case AUTH_STATE_VALIDATING:
				text = "verifying";
				break;
			case AUTH_STATE_INVALID:
				text = "wrong";
				break;
			case AUTH_STATE_CLEAR:
				text = "cleared";
				break;
			case AUTH_STATE_INPUT:
			case AUTH_STATE_INPUT_NOP:
			case AUTH_STATE_BACKSPACE:
				// Caps Lock has higher priority
				if (state->xkb.caps_lock && state->args.show_caps_lock_text) {
					text = "Caps Lock";
				} else if (state->args.show_failed_attempts &&
						state->failed_attempts > 0) {
					if (state->failed_attempts > 999) {
						text = "999+";
					} else {
						snprintf(attempts, sizeof(attempts), "%d", state->failed_attempts);
						text = attempts;
					}
				}

				xkb_layout_index_t num_layout = xkb_keymap_num_layouts(state->xkb.keymap);
				if (!state->args.hide_keyboard_layout &&
						(state->args.show_keyboard_layout || num_layout > 1)) {
					xkb_layout_index_t curr_layout = 0;

					// advance to the first active layout (if any)
					while (curr_layout < num_layout &&
						xkb_state_layout_index_is_active(state->xkb.state,
							curr_layout, XKB_STATE_LAYOUT_EFFECTIVE) != 1) {
						++curr_layout;
					}
					// will handle invalid index if none are active
					layout_text = xkb_keymap_layout_get_name(state->xkb.keymap, curr_layout);
				}
				break;
			default:
				break;
			}

			if (text) {
				cairo_text_extents_t extents;
				cairo_font_extents_t fe;
				double x, y;
				cairo_text_extents(cairo, text, &extents);
				cairo_font_extents(cairo, &fe);
				x = (buffer_width / 2) -
					(extents.width / 2 + extents.x_bearing);
				y = (buffer_diameter / 2) +
					(fe.height / 2 - fe.descent);

				cairo_move_to(cairo, x, y);
				cairo_show_text(cairo, text);
				cairo_close_path(cairo);
				cairo_new_sub_path(cairo);

				if (new_width < extents.width) {
					new_width = extents.width;
				}
			}

			// Typing indicator: Highlight random part on keypress
			if (state->auth_state == AUTH_STATE_INPUT
					|| state->auth_state == AUTH_STATE_BACKSPACE) {
				static double highlight_start = 0;
				highlight_start +=
					(rand() % (int)(M_PI * 100)) / 100.0 + M_PI * 0.5;
				cairo_arc(cairo, buffer_width / 2, buffer_diameter / 2,
						arc_radius, highlight_start,
						highlight_start + TYPE_INDICATOR_RANGE);
				if (state->auth_state == AUTH_STATE_INPUT) {
					if (state->xkb.caps_lock && state->args.show_caps_lock_indicator) {
						cairo_set_source_u32(cairo, state->args.colors.caps_lock_key_highlight);
					} else {
						cairo_set_source_u32(cairo, state->args.colors.key_highlight);
					}
				} else {
					if (state->xkb.caps_lock && state->args.show_caps_lock_indicator) {
						cairo_set_source_u32(cairo, state->args.colors.caps_lock_bs_highlight);
					} else {
						cairo_set_source_u32(cairo, state->args.colors.bs_highlight);
					}
				}
				cairo_stroke(cairo);

				// Draw borders
				cairo_set_source_u32(cairo, state->args.colors.separator);
				cairo_arc(cairo, buffer_width / 2, buffer_diameter / 2,
						arc_radius, highlight_start,
						highlight_start + type_indicator_border_thickness);
				cairo_stroke(cairo);

				cairo_arc(cairo, buffer_width / 2, buffer_diameter / 2,
						arc_radius, highlight_start + TYPE_INDICATOR_RANGE,
						highlight_start + TYPE_INDICATOR_RANGE +
							type_indicator_border_thickness);
				cairo_stroke(cairo);
			}

			// Draw inner + outer border of the circle
			set_color_for_state(cairo, state, &state->args.colors.line);
			cairo_set_line_width(cairo, 2.0 * surface->scale);
			cairo_arc(cairo, buffer_width / 2, buffer_diameter / 2,
					arc_radius - arc_thickness / 2, 0, 2 * M_PI);
			cairo_stroke(cairo);
			cairo_arc(cairo, buffer_width / 2, buffer_diameter / 2,
					arc_radius + arc_thickness / 2, 0, 2 * M_PI);
			cairo_stroke(cairo);

			// display layout text seperately
			if (layout_text) {
				cairo_text_extents_t extents;
				cairo_font_extents_t fe;
				double x, y;
				double box_padding = 4.0 * surface->scale;
				cairo_text_extents(cairo, layout_text, &extents);
				cairo_font_extents(cairo, &fe);
				// upper left coordinates for box
				x = (buffer_width / 2) - (extents.width / 2) - box_padding;
				y = buffer_diameter;

				// background box
				cairo_rectangle(cairo, x, y,
					extents.width + 2.0 * box_padding,
					fe.height + 2.0 * box_padding);
				cairo_set_source_u32(cairo, state->args.colors.layout_background);
				cairo_fill_preserve(cairo);
				// border
				cairo_set_source_u32(cairo, state->args.colors.layout_border);
				cairo_stroke(cairo);

				// take font extents and padding into account
				cairo_move_to(cairo,
					x - extents.x_bearing + box_padding,
					y + (fe.height - fe.descent) + box_padding);
				cairo_set_source_u32(cairo, state->args.colors.layout_text);
				cairo_show_text(cairo, layout_text);
				cairo_new_sub_path(cairo);

				new_height += fe.height + 2 * box_padding;
				if (new_width < extents.width + 2 * box_padding) {
					new_width = extents.width + 2 * box_padding;
				}
			}

			if (buffer_width != new_width || buffer_height != new_height) {
				destroy_buffer(surface->current_buffer);
				surface->indicator_width = new_width;
				surface->indicator_height = new_height;
				render_frame(surface);
			}
		}
	}

	wl_surface_set_buffer_scale(surface->child, surface->scale);
	wl_surface_attach(surface->child, surface->current_buffer->buffer, 0, 0);
	wl_surface_damage(surface->child, 0, 0, surface->current_buffer->width, surface->current_buffer->height);
	wl_surface_commit(surface->child);

	wl_surface_commit(surface->surface);
}

void render_frames(struct swaylock_state *state) {
	struct swaylock_surface *surface;
	wl_list_for_each(surface, &state->surfaces, link) {
		render_frame(surface);
	}
}
