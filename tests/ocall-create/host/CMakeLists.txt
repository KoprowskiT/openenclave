# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.

add_executable(ocall_create_host host.c)
target_link_libraries(ocall_create_host oehostapp)
