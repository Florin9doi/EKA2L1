/*
 * Copyright (c) 2018 EKA2L1 Team.
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

#include <common/algorithm.h>
#include <common/buffer.h>
#include <common/cvt.h>
#include <common/flate.h>
#include <common/fileutils.h>
#include <common/log.h>
#include <common/path.h>
#include <common/types.h>

#include <vfs/vfs.h>
#include <config/config.h>

#include <package/manager.h>
#include <package/sis_script_interpreter.h>

#include <miniz.h>

namespace eka2l1 {
    namespace loader {
        std::string get_install_path(const std::u16string &pseudo_path, drive_number drv) {
            std::u16string raw_path = pseudo_path;

            if (raw_path.substr(0, 2) == u"!:") {
                raw_path[0] = drive_to_char16(drv);
            }

            return common::ucs2_to_utf8(raw_path);
        }

        bool ss_interpreter::appprop(sis_uid uid, sis_property prop) {
            return false;
        }

        bool ss_interpreter::package(sis_uid uid) {
            if (mngr->installed(uid.uid)) {
                return true;
            }

            return false;
        }

        ss_interpreter::ss_interpreter() {
        }

        ss_interpreter::ss_interpreter(common::ro_stream *stream,
            io_system *io,
            manager::packages *pkgmngr,
            sis_controller *main_controller,
            sis_data *inst_data,
            drive_number inst_drv)
            : data_stream(stream)
            , mngr(pkgmngr)
            , io(io)
            , main_controller(main_controller)
            , install_data(inst_data)
            , install_drive(inst_drv) {
        }

        std::vector<uint8_t> ss_interpreter::get_small_file_buf(uint32_t data_idx, uint16_t crr_blck_idx) {
            sis_file_data *data = reinterpret_cast<sis_file_data *>(
                reinterpret_cast<sis_data_unit *>(install_data->data_units.fields[crr_blck_idx].get())->data_unit.fields[data_idx].get());
            sis_compressed compressed = data->raw_data;

            std::uint64_t us = ((compressed.len_low) | (static_cast<uint64_t>(compressed.len_high) << 32)) - 12;

            compressed.compressed_data.resize(us);

            data_stream->seek(compressed.offset, common::seek_where::beg);
            data_stream->read(&compressed.compressed_data[0], us);

            if (compressed.algorithm == sis_compressed_algorithm::none) {
                return compressed.compressed_data;
            }

            compressed.uncompressed_data.resize(compressed.uncompressed_size);
            mz_stream stream;

            stream.zalloc = nullptr;
            stream.zfree = nullptr;

            if (inflateInit(&stream) != MZ_OK) {
                LOG_ERROR(PACKAGE, "Can not intialize inflate stream");
            }

            flate::inflate_data(&stream, compressed.compressed_data.data(),
                compressed.uncompressed_data.data(), static_cast<std::uint32_t>(us));

            inflateEnd(&stream);

            return compressed.uncompressed_data;
        }

        // Assuming this file is small since it's stored in std::vector
        // Directly write this
        void extract_file_with_buf(const std::string &path, std::vector<uint8_t> &data) {
            std::string rp = eka2l1::file_directory(path);
            eka2l1::create_directories(rp);

            LOG_INFO(PACKAGE, "Write to: {}", path);

            FILE *temp = fopen(path.c_str(), "wb");
            fwrite(data.data(), 1, data.size(), temp);

            fclose(temp);
        }

        void ss_interpreter::extract_file(const std::string &path, const uint32_t idx, uint16_t crr_blck_idx) {
            std::string rp = eka2l1::file_directory(path);
            eka2l1::create_directories(rp);

            // Delete the file, starts over
            if (common::is_system_case_insensitive() && eka2l1::exists(path)) {
                if (!common::remove(path)) {
                    LOG_WARN(PACKAGE, "Unable to remove {} to extract new file", path);
                }
            }

            FILE *file = fopen(path.c_str(), "wb");

            sis_data_unit *data_unit = reinterpret_cast<sis_data_unit *>(install_data->data_units.fields[crr_blck_idx].get());
            sis_file_data *data = reinterpret_cast<sis_file_data *>(data_unit->data_unit.fields[idx].get());

            sis_compressed compressed = data->raw_data;

            std::uint64_t left = ((compressed.len_low) | (static_cast<std::uint64_t>(compressed.len_high) << 32)) - 12;
            data_stream->seek(compressed.offset, common::seek_where::beg);

            std::vector<unsigned char> temp_chunk;
            temp_chunk.resize(CHUNK_SIZE);

            std::vector<unsigned char> temp_inflated_chunk;
            temp_inflated_chunk.resize(CHUNK_MAX_INFLATED_SIZE);

            mz_stream stream;

            stream.zalloc = nullptr;
            stream.zfree = nullptr;

            if (compressed.algorithm == sis_compressed_algorithm::deflated) {
                if (inflateInit(&stream) != MZ_OK) {
                    LOG_ERROR(PACKAGE, "Can not intialize inflate stream");
                }
            }

            std::uint32_t total_inflated_size = 0;

            while (left > 0) {
                std::fill(temp_chunk.begin(), temp_chunk.end(), 0);
                int grab = static_cast<int>(left < CHUNK_SIZE ? left : CHUNK_SIZE);

                data_stream->read(&temp_chunk[0], grab);

                if (!data_stream->valid()) {
                    LOG_ERROR(PACKAGE, "Stream fail, skipping this file, should report to developers.");
                    return;
                }

                if (compressed.algorithm == sis_compressed_algorithm::deflated) {
                    uint32_t inflated_size = 0;

                    auto res = flate::inflate_data(&stream, temp_chunk.data(), temp_inflated_chunk.data(), grab, &inflated_size);

                    if (!res) {
                        LOG_ERROR(PACKAGE, "Uncompress failed! Report to developers");
                        return;
                    }

                    fwrite(temp_inflated_chunk.data(), 1, inflated_size, file);
                    total_inflated_size += inflated_size;
                } else {
                    fwrite(temp_chunk.data(), 1, grab, file);
                }

                left -= grab;
            }

            if (compressed.algorithm == sis_compressed_algorithm::deflated) {
                if (total_inflated_size != compressed.uncompressed_size) {
                    LOG_ERROR(PACKAGE, "Sanity check failed: Total inflated size not equal to specified uncompress size "
                              "in SISCompressed ({} vs {})!",
                        total_inflated_size, compressed.uncompressed_size);
                }

                inflateEnd(&stream);
            }

            fclose(file);
        }

        static bool is_expression_integral_type(const ss_expr_op op) {
            return (op == ss_expr_op::EPrimTypeNumber)
                || (op == ss_expr_op::EPrimTypeOption)
                || (op == ss_expr_op::EPrimTypeVariable);
        }

        int ss_interpreter::gasp_true_form_of_integral_expression(const sis_expression &expr) {
            switch (expr.op) {
            case ss_expr_op::EPrimTypeVariable: {
                switch (expr.int_val) {
                // Language variable. We choosen upper
                case 0x1000: {
                    return static_cast<int>(current_controllers.top()->chosen_lang);
                }

                default:
                    break;
                }

                if (var_resolver) {
                    return var_resolver(expr.int_val);
                }

                return 0;
            }

            default:
                break;
            }

            return expr.int_val;
        }

        int ss_interpreter::condition_passed(sis_expression *expr) {
            if (!expr || expr->type != sis_field_type::SISExpression) {
                return -1;
            }

            int pass = 0;

            if ((expr->left_expr && (expr->left_expr->op == ss_expr_op::EPrimTypeString)) ||
                (expr->right_expr && (expr->right_expr->op == ss_expr_op::EPrimTypeString))) {
                if (expr->left_expr->op != expr->right_expr->op) {
                    LOG_ERROR(PACKAGE, "String expression can only be compared with string expression");
                    return -1;
                }

                switch (expr->op) {
                case ss_expr_op::EBinOpEqual: {
                    pass = (expr->left_expr->val.unicode_string == expr->right_expr->val.unicode_string);
                    break;
                }

                case ss_expr_op::EBinOpNotEqual: {
                    pass = (expr->left_expr->val.unicode_string != expr->right_expr->val.unicode_string);
                    break;
                }

                case ss_expr_op::EBinOpGreaterThan: {
                    pass = (expr->left_expr->val.unicode_string > expr->right_expr->val.unicode_string);
                    break;
                }

                case ss_expr_op::EBinOpLessThan: {
                    pass = (expr->left_expr->val.unicode_string < expr->right_expr->val.unicode_string);
                    break;
                }

                case ss_expr_op::EBinOpGreaterThanOrEqual: {
                    pass = (expr->left_expr->val.unicode_string >= expr->right_expr->val.unicode_string);
                    break;
                }

                case ss_expr_op::EBinOpLessOrEqual: {
                    pass = (expr->left_expr->val.unicode_string <= expr->right_expr->val.unicode_string);
                    break;
                }

                default: {
                    LOG_WARN(PACKAGE, "Unhandled string op type: {}", static_cast<int>(expr->op));
                    pass = -1;
                    break;
                }
                }

                return pass;
            }

            const int lhs = condition_passed(expr->left_expr.get());
            const int rhs = condition_passed(expr->right_expr.get());

            switch (expr->op) {
            case ss_expr_op::EBinOpEqual: {
                pass = (lhs == rhs);
                break;
            }

            case ss_expr_op::EBinOpNotEqual: {
                pass = (lhs != rhs);
                break;
            }

            case ss_expr_op::EBinOpGreaterThan: {
                pass = (lhs > rhs);
                break;
            }

            case ss_expr_op::EBinOpLessThan: {
                pass = (lhs < rhs);
                break;
            }

            case ss_expr_op::EBinOpGreaterThanOrEqual: {
                pass = (lhs >= rhs);
                break;
            }

            case ss_expr_op::EBinOpLessOrEqual: {
                pass = (lhs <= rhs);
                break;
            }

            case ss_expr_op::ELogOpAnd: {
                pass = static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs);
                break;
            }

            case ss_expr_op::ELogOpOr: {
                pass = static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs);
                break;
            }

            case ss_expr_op::EPrimTypeNumber:
            case ss_expr_op::EPrimTypeVariable: {
                pass = gasp_true_form_of_integral_expression(*expr);
                break;
            }

            case ss_expr_op::EUnaryOpNot: {
                pass = !lhs;
                break;
            }

            case ss_expr_op::EFuncExists: {
                pass = io->exist(expr->val.unicode_string);
                break;
            }

            default: {
                pass = -1;
                LOG_WARN(PACKAGE, "Unimplemented operation {} for expression", static_cast<int>(expr->op));
                break;
            }
            }

            return pass;
        }

        bool ss_interpreter::interpret(sis_controller *controller, const std::uint16_t base_data_idx, std::atomic<int> &progress) {
            // Set current controller
            current_controllers.push(controller);

            // Ask for language. If we can't choose the first one, or none
            if (controller->langs.langs.fields.size() != 1 && choose_lang) {
                std::vector<int> langs;

                for (auto &lang_field : controller->langs.langs.fields) {
                    langs.push_back(static_cast<int>(reinterpret_cast<sis_language *>(lang_field.get())->language));
                }

                controller->chosen_lang = static_cast<sis_lang>(choose_lang(&langs[0], static_cast<int>(langs.size())));

                if (controller->chosen_lang == static_cast<sis_lang>(-1)) {
                    std::string error_string = fmt::format("Abort choosing language, leading to installation canceled for controller 0x{:X}!",
                        controller->info.uid.uid);

                    LOG_ERROR(PACKAGE, "{}", error_string);

                    if (show_text) {
                        show_text(error_string.c_str(), true);
                    }

                    return false;
                }
            } else {
                controller->chosen_lang = reinterpret_cast<sis_language *>(controller->langs.langs.fields[0].get())->language;
            }

            // TODO: Choose options
            const bool result = interpret(controller->install_block, progress, base_data_idx + controller->idx.data_index);
            current_controllers.pop();
            
            return result;
        }

        bool ss_interpreter::interpret(sis_install_block &install_block, std::atomic<int> &progress, uint16_t crr_blck_idx) {
            // Process file
            auto install_file = [&](sis_install_block &inst_blck, uint16_t crr_blck_idx) {
                for (auto &wrap_file : inst_blck.files.fields) {
                    sis_file_des *file = (sis_file_des *)(wrap_file.get());
                    std::string raw_path = "";
                    std::string install_path = "";

                    if (file->target.unicode_string.length() > 0) {
                        install_path = get_install_path(file->target.unicode_string, install_drive);
                        raw_path = common::ucs2_to_utf8(*(io->get_raw_path(common::utf8_to_ucs2(install_path))));
                    }

                    switch (file->op) {
                    case ss_op::EOpText: {
                        auto buf = get_small_file_buf(file->idx, crr_blck_idx);
                        buf.push_back(0);

                        bool yes_choosen = true;

                        if (show_text) {
                            show_text(reinterpret_cast<const char *>(buf.data()), true);
                        }

                        LOG_INFO(PACKAGE, "EOpText: {}", reinterpret_cast<const char *>(buf.data()));

                        switch (file->op_op) {
                        case 1 << 10: { // Skip next files
                            if (show_text) {
                                yes_choosen = show_text("Skip next file?", false);
                            }

                            if (yes_choosen) {
                                skip_next_file = true;
                            }
                            break;
                        }

                        case 1 << 11:
                        case 1 << 12: { // Abort
                            const std::string err_string = fmt::format("Continue the installation for this package? (0x{:X})", current_controllers.top()->info.uid.uid);

                            LOG_ERROR(PACKAGE, "{}", err_string);

                            if (show_text) {
                                yes_choosen = show_text(err_string.c_str(), false);
                            }

                            if (!yes_choosen) {
                                mngr->delete_files_and_bucket(current_controllers.top()->info.uid.uid);
                            }

                            break;
                        }

                        default: {
                            break;
                        }
                        }

                        break;
                    }

                    case ss_op::EOpInstall:
                    case ss_op::EOpNull: {
                        if (!skip_next_file) {
                            bool lowered = false;

                            if (common::is_platform_case_sensitive()) {
                                raw_path = common::lowercase_string(raw_path);
                                lowered = true;
                            }

                            extract_file(raw_path, file->idx, crr_blck_idx);

                            LOG_INFO(PACKAGE, "EOpInstall: {}", raw_path);

                            // Add to bucket
                            mngr->add_to_file_bucket(current_controllers.top()->info.uid.uid, install_path);

                            if (!lowered) {
                                raw_path = common::lowercase_string(raw_path);
                            }

                            if (FOUND_STR(raw_path.find(".sis")) || FOUND_STR(raw_path.find(".sisx"))) {
                                LOG_INFO(PACKAGE, "Detected an SmartInstaller SIS, path at: {}", raw_path);
                                mngr->install_package(common::utf8_to_ucs2(raw_path), drive_c, progress);
                            }
                        } else {
                            skip_next_file = false;
                        }

                        break;
                    }

                    default:
                        break;
                    }
                }
            };

            install_file(install_block, crr_blck_idx);

            // Parse if blocks
            for (auto &wrap_if_statement : install_block.if_blocks.fields) {
                sis_if *if_stmt = (sis_if *)(wrap_if_statement.get());
                auto result = condition_passed(&if_stmt->expr);

                if (result) {
                    interpret(if_stmt->install_block, progress, crr_blck_idx);
                } else {
                    for (auto &wrap_else_branch : if_stmt->else_if.fields) {
                        sis_else_if *else_branch = (sis_else_if *)(wrap_else_branch.get());

                        if (condition_passed(&else_branch->expr)) {
                            interpret(else_branch->install_block, progress, crr_blck_idx);
                            break;
                        }
                    }
                }
            }

            for (auto &wrap_mini_pkg : install_block.controllers.fields) {
                sis_controller *ctrl = (sis_controller *)(wrap_mini_pkg.get());
                interpret(ctrl, crr_blck_idx, progress);
            }

            return true;
        }
    }
}
