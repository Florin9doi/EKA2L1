/*
 * Copyright (c) 2020 EKA2L1 Team
 * 
 * This file is part of EKA2L1 project
 * (see bentokun.github.com/EKA2L1).
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <common/uid.h>
#include <utils/des.h>

#include <kernel/server.h>
#include <services/framework.h>

namespace eka2l1 {
    namespace common {
        class chunkyseri;
    }

    enum sisregistry_opcode {
        sisregistry_open_registry_uid = 0x0,
        sisregistry_close_registry_entry = 0x3,
        sisregistry_version = 0x5,
        sisregistry_localized_vendor_name = 0x8,
        sisregistry_package_name = 0x9,
        sisregistry_in_rom = 0xA,
        sisregistry_selected_drive = 0xD,
        sisregistry_get_matching_supported_languages = 0x10,
        sisregistry_trust_timestamp = 0x16,
        sisregistry_trust_status_op = 0x17,
        sisregistry_sid_to_filename = 0x19,
        sisregistry_package_exists_in_rom = 0x1D,
        sisregistry_stub_file_entries = 0x1E,
        sisregistry_separator_minimum_read_user_data = 0x20,
        sisregistry_installed_packages = 0x22,
        sisregistry_sid_to_package = 0x23,
        sisregistry_installed_uid = 0x24,
        sisregistry_uid = 0x29,
        sisregistry_get_entry = 0x2A,
        sisregistry_sids = 0x2D,
        sisregistry_files = 0x2E,
        sisregistry_file_descriptions = 0x2F,
        sisregistry_package_augmentations = 0x30,
        sisregistry_preinstalled = 0x32,
        sisregistry_package_op = 0x34,
        sisregistry_non_removable = 0x36,
        sisregistry_add_entry = 0x41,
        sisregistry_update_entry = 0x42,
        sisregistry_delete_entry = 0x43,
        sisregistry_install_type_op = 0x44,    
        sisregistry_regenerate_cache = 0x45,
        sisregistry_dependent_packages = 0x103,
        sisregistry_embedded_packages = 0x104,
        sisregistry_signed_by_sucert = 0x107
    };

    enum sisregistry_stub_extraction_mode {
        sisregistry_stub_extraction_mode_get_count,
        sisregistry_stub_extraction_mode_get_files
    };

    class sisregistry_server : public service::typical_server {
    public:
        bool added = false;

        explicit sisregistry_server(eka2l1::system *sys);

        void connect(service::ipc_context &context) override;
    };

    struct sisregistry_client_session : public service::typical_session {
    public:
        explicit sisregistry_client_session(service::typical_server *serv, const kernel::uid ss_id, epoc::version client_version);

        void fetch(service::ipc_context *ctx) override;
        void open_registry_uid(eka2l1::service::ipc_context *ctx);
        void get_version(eka2l1::service::ipc_context *ctx);
        void is_in_rom(eka2l1::service::ipc_context *ctx);
        void get_selected_drive(eka2l1::service::ipc_context *ctx);
        void request_files(eka2l1::service::ipc_context *ctx);
        void request_uid(eka2l1::service::ipc_context *ctx);
        void get_entry(eka2l1::service::ipc_context *ctx);
        void request_stub_file_entries(eka2l1::service::ipc_context *ctx);
        void populate_files(common::chunkyseri &seri);
        void request_file_descriptions(eka2l1::service::ipc_context *ctx);
        void populate_file_descriptions(common::chunkyseri &seri);
        void request_package_augmentations(eka2l1::service::ipc_context *ctx);
        void populate_augmentations(common::chunkyseri &seri);
        void is_non_removable(eka2l1::service::ipc_context *ctx);
        void add_entry(eka2l1::service::ipc_context *ctx);
        void is_preinstalled(eka2l1::service::ipc_context *ctx);
        void get_package(eka2l1::service::ipc_context *ctx);
        void get_trust_timestamp(eka2l1::service::ipc_context *ctx);
        void get_trust_status(eka2l1::service::ipc_context *ctx);
        void request_sid_to_filename(eka2l1::service::ipc_context *ctx);
        void is_signed_by_sucert(eka2l1::service::ipc_context *ctx);
        void is_installed_uid(eka2l1::service::ipc_context *ctx);
        void sid_to_package(eka2l1::service::ipc_context *ctx);
        void populate_sids(common::chunkyseri &seri);
        void request_sids(eka2l1::service::ipc_context *ctx);
    };
}
