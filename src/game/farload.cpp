#include "pch.h"

namespace farload {

    uint32_t farload_file_flag;
    char farload_file_name[16];
    FILE* farload_fptr;
    s_lbx_header lbx_header;
    char secondary_drive_path[52];

    void* __cdecl Farload_Library(const char* lib_name, int32_t entry_idx, void* allocation_base, int32_t allocation_mode) {
        void* dest_buf = nullptr;
        s_vfs_file* file;
        int32_t size;

        if (entry_idx < 0) {
            farload::Error_Handler(lib_name, 4, entry_idx);
        }

        file = vfs::fopen_lbx(lib_name, entry_idx);
        if (file == nullptr) {
            farload::Error_Handler(lib_name, 1, entry_idx);
        }

        size = vfs::fsize(file);
        if (size < 0) {
            vfs::fclose(file);
            farload::Error_Handler(lib_name, 7, entry_idx);
        }

        if (allocation_mode == 0) {
            dest_buf = allocate::Allocate_Space_No_Header(size);
            if (dest_buf == nullptr) {
                vfs::fclose(file);
                farload::Error_Handler(lib_name, 3, entry_idx);
            }
        } else if (allocation_mode == 1) {
            dest_buf = allocate::Allocate_First_Block_No_Header(allocation_base, size);
            if (dest_buf == nullptr) {
                vfs::fclose(file);
                farload::Error_Handler(lib_name, 5, entry_idx);
            }
        } else if (allocation_mode == 2) {
            dest_buf = allocate::Allocate_Next_Block_No_Header(allocation_base, size);
            if (dest_buf == nullptr) {
                vfs::fclose(file);
                farload::Error_Handler(lib_name, 5, entry_idx);
            }
        } else {
            vfs::fclose(file);
            farload::Error_Handler(lib_name, 7, entry_idx);
        }

        if (vfs::fread(dest_buf, (size_t)size, 1, file) != 1) {
            vfs::fclose(file);
            farload::Error_Handler(lib_name, 7, entry_idx);
        }

        vfs::fclose(file);
        render::Register_Load(dest_buf, size, lib_name, entry_idx);
        return dest_buf;
    }

    void* __cdecl Far_Reload(const char* path, int32_t entry_idx, void* allocation_base) {
        return Farload_Library(path, entry_idx, allocation_base, 1);
    }

    void* __cdecl Far_Reload_Next(const char* filename, int32_t entry_idx, void* buffer) {
        return Farload_Library(filename, entry_idx, buffer, 2);
    }

    void* __cdecl Farload(char* file_name, int32_t entry_idx) {
        return Farload_Library(file_name, entry_idx, nullptr, 0);
    }

    void* __cdecl Farload_Library_Data(const char* library_name, int32_t entry_idx, void* buffer, int16_t allocation_mode, uint32_t start_idx, uint16_t record_count, uint16_t expected_element_size) {
        s_vfs_file* file;
        uint16_t total_count;
        uint16_t element_size;
        uint32_t read_size;

        if (entry_idx < 0) {
            Error_Handler(library_name, 4, entry_idx);
        }

        file = vfs::fopen_lbx(library_name, entry_idx);
        if (file == nullptr) {
            Error_Handler(library_name, 1, entry_idx);
        }

        if (vfs::fread(&total_count, sizeof(total_count), 1, file) != 1 ||
            vfs::fread(&element_size, sizeof(element_size), 1, file) != 1) {
            vfs::fclose(file);
            Error_Handler(library_name, 7, entry_idx);
        }

        if (record_count == 0xFFFFu) {
            record_count = (uint16_t)(total_count - start_idx);
        }

        if (total_count <= start_idx) {
            Error_Handler(library_name, 10, entry_idx);
        }

        if (element_size != expected_element_size) {
            vfs::fclose(file);
            Error_Handler(library_name, 9, entry_idx);
        }

        if (vfs::fseek(file, (int32_t)(sizeof(total_count) + sizeof(element_size) + element_size * start_idx), SEEK_SET) != 0) {
            vfs::fclose(file);
            Error_Handler(library_name, 7, entry_idx);
        }

        read_size = element_size * record_count;

        switch (allocation_mode) {
            case 0:
                buffer = allocate::Allocate_Space_No_Header(read_size);
                if (buffer == nullptr) {
                    vfs::fclose(file);
                    Error_Handler(library_name, 3, entry_idx);
                }
                break;
            case 1:
                buffer = allocate::Allocate_First_Block_No_Header(buffer, read_size);
                if (buffer == nullptr) {
                    vfs::fclose(file);
                    Error_Handler(library_name, 5, entry_idx);
                }
                break;
            case 2:
                buffer = allocate::Allocate_Next_Block_No_Header(buffer, read_size);
                if (buffer == nullptr) {
                    vfs::fclose(file);
                    Error_Handler(library_name, 5, entry_idx);
                }
                break;
            case 3:
                break;
            default:
                vfs::fclose(file);
                Error_Handler(library_name, 7, entry_idx);
                break;
        }

        if (vfs::fread(buffer, read_size, 1, file) != 1) {
            vfs::fclose(file);
            Error_Handler(library_name, 7, entry_idx);
        }

        vfs::fclose(file);

        return buffer;
    }

    void* __cdecl Far_Reload_Data(const char* path, int32_t entry_idx, void* buffer, uint32_t start_idx, uint16_t record_count, uint16_t expected_element_size) {
        return Farload_Library_Data(path, entry_idx, buffer, 1, start_idx, record_count, expected_element_size);
    }

    void* __cdecl Far_Reload_Next_Data(const char* filename, int32_t entry_idx, void* buffer, uint32_t start_idx, uint16_t record_count, uint16_t expected_element_size) {
        return Farload_Library_Data(filename, entry_idx, buffer, 2, start_idx, record_count, expected_element_size);
    }

    void __cdecl Error_Handler(const char* filename, int32_t error_code, int32_t entry_num) {
        char message[120];
        const char* detail = "";

        if (error_code >= 1 && error_code <= 16) {
            switch (error_code) {
                case 1: detail = " could not be found."; break;
                case 2: detail = " has been corrupted."; break;
                case 3:
                    strlcpy(message, "Insufficient memory. You need at least 575K free. Try removing all TSR's.", sizeof(message));
                    exitm::Exit_With_Message(message);
                    return;
                case 4: detail = " has an improper entry number."; break;
                case 5: detail = " failed to reload. Allocation too small"; break;
                case 7: detail = " is not an LBX file"; break;
                case 8: detail = " exceeds number of LBX entries"; break;
                case 9: detail = " has an incorrect record size"; break;
                case 10: detail = " exceeds number of defined records"; break;
                case 16: detail = " Vga file animation frames cannot exceed 65536 bytes"; break;
                default: break;
            }
        }

        snprintf(message, sizeof(message), "%s [entry %d]%s", filename, (int)entry_num, detail);

        exitm::Exit_With_Message(message);
    }

    int16_t __cdecl Get_Farload_Data_Record_Count(const char* file_name, int32_t entry_idx) {
        s_vfs_file* file;
        int16_t record_count;

        if (entry_idx < 0) {
            Error_Handler(file_name, 4, entry_idx);
        }

        file = vfs::fopen_lbx(file_name, entry_idx);
        if (file == nullptr) {
            Error_Handler(file_name, 1, entry_idx);
        }

        if (vfs::fread(&record_count, sizeof(record_count), 1, file) != 1) {
            vfs::fclose(file);
            Error_Handler(file_name, 7, entry_idx);
        }

        vfs::fclose(file);

        return record_count;
    }

    void* __cdecl Farload_Data_Static(const char* filename, int32_t resource_id, void* buffer, int32_t start_idx, int32_t record_count, int32_t expected_element_size) {
        return farload::Farload_Library_Data(filename, resource_id, buffer, 3, (uint32_t)start_idx, (uint16_t)record_count, (uint16_t)expected_element_size);
    }

    void __cdecl Set_Alternate_Path(char* input_path) {
        if (!Cleanup_Path(input_path, secondary_drive_path, sizeof(secondary_drive_path), true)) {
            secondary_drive_path[0] = '\0';
        }
    }

} // namespace farload
