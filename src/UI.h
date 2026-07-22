#pragma once

#include <imgui.h>
#include "State.h"
static bool mute = true;
static char save_dir[64];

static char cam_name[20];
static int cam_number = 0;
void createSettingsMenu(AppState *app_state);
void createGUI(AppState *app_state);
bool create_cam_windows(AppState *app_state);
void set_window_bounding(AppState *app_state, int index);
void recalculate_windows(int nb_cams);
