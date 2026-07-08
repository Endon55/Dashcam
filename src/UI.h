#pragma once

#include <imgui.h>
#include "State.h"
#include "Camera/Camera.h"
#include "Camera/WebcamUtils.h"
#include "Settings.h"

static bool mute = true;
static char save_dir[64];

static char cam_name[20];

void createSettingsMenu(AppState *app_state);
void createGUI(AppState *app_state);


