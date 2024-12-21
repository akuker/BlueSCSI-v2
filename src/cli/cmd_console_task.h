// Periodic FreeRTOS task that will run the FreeRTOS CLI framework. Since this
// runs asynchronously with the rest of the tasks, pay attention to thread-
// safety.
//
// Copyright (C) 2024 akuker
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <https://www.gnu.org/licenses/>.
//
#pragma once
#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FREERTOS_CLI_MAX_INPUT_LENGTH 50
#define FREERTOS_CLI_MAX_OUTPUT_LENGTH 100


void vCommandConsoleTask( void *pvParameters );

#ifdef __cplusplus
}
#endif