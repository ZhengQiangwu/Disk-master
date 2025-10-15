// disk_cleaner.cpp
#include "disk_cleaner.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <unordered_set>
#include <cstring>
#include <algorithm> // <--- 添加此行
#include <cctype>    // <--- 添加此行

namespace fs = std::filesystem;

// --- 新增：线程安全的停止标志 ---
static std::atomic<bool> g_stop_scan_flag(false);

// --- 内部数据结构和状态变量 ---
static std::thread g_scan_thread;
static std::atomic<bool> g_scan_finished(true);
static std::mutex g_results_mutex;
static std::vector<FileInfo> g_trash_files;
static std::vector<FileInfo> g_package_files;
static std::vector<FileInfo> g_compressed_files; // 压缩包
static std::vector<FileInfo> g_video_files;
static std::vector<FileInfo> g_audio_files;
static std::vector<FileInfo> g_image_files; // 图片
static std::vector<FileInfo> g_document_files; // 文档
static std::atomic<uint64_t> g_total_junk_size(0);

// --- 文件类型定义 ---
std::unordered_set<std::string> g_package_exts = {".deb", ".rpm", ".pkg", ".appimage"};
std::unordered_set<std::string> g_video_exts = {".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm",
 								".3gp", ".m4v", ".mpg", ".rmvb", ".rm", ".vob", ".mpeg"};
std::unordered_set<std::string> g_audio_exts = {".mp3", ".wav", ".flac", ".aac", ".ogg", ".m4a", ".wma"};
std::unordered_set<std::string> g_image_exts = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".tiff", ".svg", ".webp"};
std::unordered_set<std::string> g_document_exts = {".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx"};//文档类别

// --- 核心修改：使用一个有序的 vector 来检查文件名结尾 ---
// 把更长的、更精确的后缀放在前面
std::vector<std::string> g_compressed_endings = {
    ".tar.gz", ".tar.bz2", ".tar.xz", ".tgz",
    ".zip", ".rar", ".7z", ".gz", ".bz2", ".xz", ".tar"
};

// --- 内部辅助函数 ---
// --- 更新 get_file_category 函数以支持新枚举和图片 ---
FileCategory get_file_category(const fs::path& path, const fs::path& trash_path) {
    // 获取完整文件名并转为小写，以便进行不区分大小写的比较
    std::string filename = path.filename().string();
    std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
    // 2. 检查压缩文件 (新逻辑)
    for (const auto& ending : g_compressed_endings) {
        // 手动实现 "ends_with" (兼容 C++17)
        if (filename.length() >= ending.length() && 
            filename.compare(filename.length() - ending.length(), ending.length(), ending) == 0) {
            return CATEGORY_COMPRESSED;
        }
    }
    // 3. 检查其他类型的文件
    std::string ext = path.extension().string();
    // 再次转换，因为 ext 是新获取的
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (g_package_exts.count(ext)) return CATEGORY_PACKAGES;
    if (g_video_exts.count(ext)) return CATEGORY_VIDEO;
    if (g_audio_exts.count(ext)) return CATEGORY_AUDIO;
    if (g_image_exts.count(ext)) return CATEGORY_IMAGE;
    if (g_document_exts.count(ext)) return CATEGORY_DOCUMENT;

    // 4. 如果都不是，则返回 UNKNOWN
    return CATEGORY_UNKNOWN;
}
// --- 新增：内部辅助函数，用于计算目录大小 ---
static uint64_t calculate_directory_size(const fs::path& p) {
    uint64_t current_size = 0;
    try {
        // 确保目录存在
        if (fs::exists(p) && fs::is_directory(p)) {
             for (const auto& entry : fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied)) {
                if (entry.is_regular_file()) {
                    // 同样需要捕获可能的错误
                    std::error_code ec;
                    uint64_t file_size = fs::file_size(entry, ec);
                    if (!ec) {
                        current_size += file_size;
                    }
                }
            }
        }
    } catch (...) {
        // 捕获所有可能的异常，保证函数不会崩溃
    }
    return current_size;
}

// --- 新增辅助函数：将文件处理逻辑提取出来，避免代码重复 ---
void process_file_entry(const fs::path& current_path, FileCategory category, ScanCallback callback) {
    std::error_code ec;
    uint64_t file_size = fs::file_size(current_path, ec);
    
    if (!ec) {
        char* path_copy = new char[current_path.string().length() + 1];
        strcpy(path_copy, current_path.string().c_str());
        FileInfo info = { path_copy, file_size, category };
        
        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            if (category == CATEGORY_PACKAGES) g_package_files.push_back(info);
            else if (category == CATEGORY_COMPRESSED) g_compressed_files.push_back(info);
            else if (category == CATEGORY_VIDEO) g_video_files.push_back(info);
            else if (category == CATEGORY_AUDIO) g_audio_files.push_back(info);
            else if (category == CATEGORY_IMAGE) g_image_files.push_back(info);
            else if (category == CATEGORY_DOCUMENT) g_document_files.push_back(info);
        }

        g_total_junk_size += file_size;

        if (callback) {
            callback(info.path, info.size, g_total_junk_size, info.category);
        }
    }
}

void scan_directory(const std::string& home_path_str, ScanCallback callback) {
    fs::path home_path(home_path_str);
    
    // --- 新增：定义要为搬迁类别排除的特定目录 ---
    fs::path excluded_migrate_path = home_path / "MoveFiles";

    // 清空上次扫描结果 (保持不变)
    {
        std::lock_guard<std::mutex> lock(g_results_mutex);
        g_package_files.clear();
        g_compressed_files.clear();
        g_video_files.clear();
        g_audio_files.clear();
        g_image_files.clear();
        g_trash_files.clear(); // 虽然不再扫描，但清空以保持状态一致性
        g_document_files.clear();
        g_total_junk_size = 0;
    }

    try {
        // 使用手动迭代器循环
        auto it = fs::recursive_directory_iterator(home_path, fs::directory_options::skip_permission_denied);
        auto end = fs::recursive_directory_iterator();

        while (it != end) {
            // --- 关键：在循环的开始检查停止标志 ---
            if (g_stop_scan_flag.load()) {
                std::cout << "\n[Debug] Scan stopped by request." << std::endl;
                break; // 收到停止信号，退出循环
            }
            const auto& entry = *it;
            const auto& current_path = entry.path();

            // --- 新增的核心逻辑：判断是否是隐藏文件或目录 ---
            if (current_path.filename().string().rfind('.', 0) == 0) {
                // 如果文件名以 '.' 开头
                if (entry.is_directory()) {
                    // 如果是目录，则告诉迭代器不要进入
                    it.disable_recursion_pending();
                }
                // 不论是文件还是目录，都直接跳到下一次迭代
                
                // (将迭代器递增逻辑移到循环末尾，以统一处理)
            } else {
                // --- 如果不是隐藏文件/目录，则执行之前的逻辑 ---
                if (entry.is_regular_file()) {
                    // 注意：因为隐藏目录被跳过，这里的 trash_path 参数已经无用，可以传空
                    FileCategory category = get_file_category(current_path, fs::path());
                    // --- 新增的核心逻辑：检查文件是否在排除目录内 ---
                    bool is_migrate_category = category & (CATEGORY_VIDEO | CATEGORY_AUDIO | CATEGORY_IMAGE | CATEGORY_DOCUMENT);
                    
	             if (is_migrate_category) {
	                    // 使用 weakly_canonical 进行健壮的路径比较
	                    // 检查当前文件的路径是否以排除目录的路径开头
	                    if (fs::weakly_canonical(current_path).string().rfind(excluded_migrate_path.string(), 0) == 0) {
	                        // 如果是，则跳过此文件，继续下一次循环
	                        
	                        // (将迭代器递增逻辑移到循环末尾，以统一处理)
	                    } else {
	                        // 如果不在排除目录内，则正常处理
	                        process_file_entry(current_path, category, callback);
	                    }
	                } else if (category != CATEGORY_UNKNOWN) {
	                    // 对于非搬迁类别（如安装包、压缩包），直接处理
	                    process_file_entry(current_path, category, callback);
	                }
	         }    
            }

            // --- 核心修改：手动增加迭代器 ---
            // 将迭代器递增操作也放入 try-catch 块，以处理权限问题导致的迭代失败
            try {
                ++it;
            } catch (const fs::filesystem_error& e) {
                // 如果在迭代某个目录时出错（例如，权限突然改变），则跳过它
                std::cerr << "Error iterating past " << entry.path() << ": " << e.what() << std::endl;
                it.disable_recursion_pending();
                // 再次尝试递增
                std::error_code ec;
                it.increment(ec);
                if (ec) { // 如果还是失败，就退出循环
                    break;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Scan error: " << e.what() << std::endl;
    }

    g_scan_finished = true;
}

// --- API 实现 ---
void StartScan(const char* home_path, ScanCallback callback) {
    if (!g_scan_finished) {
        return; // 扫描已在进行中
    }
    
    // --- 关键：每次开始新扫描前，必须重置停止标志 ---
    g_stop_scan_flag.store(false); 
    
    g_scan_finished = false;
    
    if (g_scan_thread.joinable()) {
        g_scan_thread.join();
    }
    g_scan_thread = std::thread(scan_directory, std::string(home_path), callback);
}

// --- 新增 API 的实现 ---
API void StopScan() {
    // 设置停止标志，通知扫描线程退出
    g_stop_scan_flag.store(true);
}

int IsScanFinished() {
    return g_scan_finished ? 1 : 0;
}

FileInfo* GetScanResults(FileCategory category, int* count) {
    std::lock_guard<std::mutex> lock(g_results_mutex);
    const std::vector<FileInfo>* source_vec = nullptr;

    // <--- MODIFIED: 使用所有新的枚举名
    if (category == CATEGORY_TRASH) source_vec = &g_trash_files;
    else if (category == CATEGORY_PACKAGES) source_vec = &g_package_files;
    else if (category == CATEGORY_VIDEO) source_vec = &g_video_files;
    else if (category == CATEGORY_AUDIO) source_vec = &g_audio_files;
    else if (category == CATEGORY_IMAGE) source_vec = &g_image_files;
    else if (category == CATEGORY_COMPRESSED) source_vec = &g_compressed_files;
    else if (category == CATEGORY_DOCUMENT) source_vec = &g_document_files;
    else {
        *count = 0;
        return nullptr;
    }

    *count = source_vec->size();
    if (*count == 0) return nullptr;
    
    // 注意：这里返回的数组内存需要调用方使用 free_scan_results 来释放
    FileInfo* results = new FileInfo[*count];
    for (int i = 0; i < *count; ++i) {
        // 浅拷贝结构体
        results[i] = (*source_vec)[i];
        // 为路径字符串分配新内存并拷贝，避免悬空指针
        results[i].path = new char[strlen((*source_vec)[i].path) + 1];
        strcpy(results[i].path, (*source_vec)[i].path);
    }
    return results;
}

void FreeScanResults(FileInfo* results, int count) {
    if (!results) return;
    // 释放 get_scan_results 中为每个 path 字符串分配的内存
    for (int i = 0; i < count; ++i) {
        delete[] results[i].path;
    }
    // 释放数组本身的内存
    delete[] results;
}
/**
 * @brief 获取回收站总大小
 * 
 * @param home_path 用户主目录路径
 * @return uint64_t 返回回收站占用的字节数
 */
static uint64_t internal_get_trash_size(const char* home_path_cstr) {
    if (!home_path_cstr) return 0;
    fs::path home_path(home_path_cstr);
    fs::path trash_files_path = home_path / ".local/share/Trash/files";
    fs::path trash_info_path = home_path / ".local/share/Trash/info";

    return calculate_directory_size(trash_files_path) + calculate_directory_size(trash_info_path);
}

int MoveFiles(const char** file_paths, int count, const char* destination_dir) {
    fs::path dest(destination_dir);
    try {
        if (!fs::exists(dest)) {
            fs::create_directories(dest);
        }
        
        for (int i = 0; i < count; ++i) {
            fs::path source(file_paths[i]);
            if (fs::exists(source)) {
                fs::path new_location = dest / source.filename();
                fs::rename(source, new_location);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to move files: " << e.what() << std::endl;
        return -1;
    }
    return 0;
}

// --- 4. 实现新的 API ---
API void SetExtensions(FileCategory category, const char* extensions[], int count) {
    // 辅助 lambda，用于填充 unordered_set
    auto update_set = [&](std::unordered_set<std::string>& target_set) {
        target_set.clear();
        for (int i = 0; i < count; ++i) {
            if (extensions[i]) {
                target_set.insert(extensions[i]);
            }
        }
    };

    switch (category) {
        case CATEGORY_PACKAGES: update_set(g_package_exts); break;
        case CATEGORY_VIDEO:    update_set(g_video_exts); break;
        case CATEGORY_AUDIO:    update_set(g_audio_exts); break;
        case CATEGORY_IMAGE:    update_set(g_image_exts); break;
        case CATEGORY_DOCUMENT: update_set(g_document_exts); break;
        case CATEGORY_COMPRESSED: // 压缩包是 vector，单独处理
            g_compressed_endings.clear();
            for (int i = 0; i < count; ++i) {
                if (extensions[i]) g_compressed_endings.emplace_back(extensions[i]);
            }
            // 为了效率，排序以将长后缀放前面
            std::sort(g_compressed_endings.begin(), g_compressed_endings.end(), [](const std::string& a, const std::string& b){
                return a.length() > b.length();
            });
            break;
        default: // 对于其他类型（如回收站、缓存），此操作无意义
            break;
    }
}

// 内部函数，实现回收站清理逻辑
static uint64_t internal_empty_trash(const std::string& home_path_str) {
    // <--- MODIFIED: 修复变量名错误
    fs::path trash_base_path = fs::path(home_path_str) / ".local/share/Trash";
    fs::path trash_files_path = trash_base_path / "files";
    fs::path trash_info_path = trash_base_path / "info";
    uint64_t freed_space = 0;

    auto calculate_size = [&](const fs::path& p) {
        uint64_t current_size = 0;
        try {
            if (fs::exists(p) && fs::is_directory(p)) {
                for (const auto& entry : fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied)) {
                    if (entry.is_regular_file()) {
                        std::error_code ec;
                        uint64_t size = fs::file_size(entry, ec);
                        if (!ec) current_size += size;
                    }
                }
            }
        } catch (...) {}
        return current_size;
    };

    freed_space = calculate_size(trash_files_path) + calculate_size(trash_info_path);

    try {
        if (fs::exists(trash_files_path)) fs::remove_all(trash_files_path);
        if (fs::exists(trash_info_path)) fs::remove_all(trash_info_path);
        fs::create_directories(trash_files_path);
        fs::create_directories(trash_info_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to empty trash: " << e.what() << std::endl;
        return 0;
    }
    return freed_space;
}

// --- 新 API 的实现 ---
API uint64_t GetSpecialCategorySize(FileCategory category) {
    const char* home_dir_cstr = getenv("HOME");
    if (!home_dir_cstr) return 0;
    fs::path home_path(home_dir_cstr);
    fs::path user_cache_path = home_path / ".cache";
    fs::path thumb_cache_path = user_cache_path / "thumbnails";

    switch (category) {
        // --- 新增 Case ---
        case CATEGORY_TRASH:
            return internal_get_trash_size(home_dir_cstr);

        case CATEGORY_THUMBNAIL_CACHE:
            return calculate_directory_size(thumb_cache_path);
        
        case CATEGORY_OTHER_APP_CACHE: {
            uint64_t total_cache_size = calculate_directory_size(user_cache_path);
            uint64_t thumb_cache_size = calculate_directory_size(thumb_cache_path);
            // 返回总大小减去缩略图大小，避免重复计算
            return (total_cache_size > thumb_cache_size) ? (total_cache_size - thumb_cache_size) : 0;
        }
        default:
            return 0;
    }
}
// --- 重构 cleanup_categories, 使其成为统一入口 ---
API uint64_t CleanupCategories(unsigned int category_mask) {
    uint64_t total_freed_space = 0;
    const char* home_dir_cstr = getenv("HOME");
    
    // --- 1. 处理缓存目录清理 (核心逻辑) ---
    if (home_dir_cstr) {
        fs::path user_cache_path = fs::path(home_dir_cstr) / ".cache";
        fs::path thumb_cache_path = user_cache_path / "thumbnails";

        // 优先处理组合情况：如果两个缓存都选了，就直接清空整个 .cache 目录
        if ((category_mask & CATEGORY_OTHER_APP_CACHE) && (category_mask & CATEGORY_THUMBNAIL_CACHE)) {
            uint64_t size = calculate_directory_size(user_cache_path);
            try {
                for (const auto& entry : fs::directory_iterator(user_cache_path)) {
                    fs::remove_all(entry.path());
                }
                total_freed_space += size;
            } catch (const fs::filesystem_error& e) { /* ... */ }
        } else { // 否则，处理单个情况
            if (category_mask & CATEGORY_THUMBNAIL_CACHE) {
                uint64_t size = calculate_directory_size(thumb_cache_path);
                try {
                    fs::remove_all(thumb_cache_path);
                    total_freed_space += size;
                } catch (const fs::filesystem_error& e) { /* ... */ }
            }
            if (category_mask & CATEGORY_OTHER_APP_CACHE) {
                // 选择性删除：遍历 .cache，但不删除 thumbnails 目录
                uint64_t freed_now = 0;
                try {
                    for (const auto& entry : fs::directory_iterator(user_cache_path)) {
                        if (entry.path() != thumb_cache_path) { // 跳过缩略图目录
                            uint64_t entry_size = calculate_directory_size(entry.path());
                            fs::remove_all(entry.path());
                            freed_now += entry_size;
                        }
                    }
                    total_freed_space += freed_now;
                } catch (const fs::filesystem_error& e) { /* ... */ }
            }
        }
    }
    
    // --- 2. 处理扫描出的文件列表清理 (复用旧逻辑) ---
    auto clear_file_list = [&](std::vector<FileInfo>& files) {
        for (const auto& file_info : files) {
            try {
                if(fs::exists(file_info.path)) {
                    fs::remove(file_info.path);
                    total_freed_space += file_info.size;
                }
            }  catch(const fs::filesystem_error& e) {
                std::cerr << "Failed to delete " << file_info.path << ": " << e.what() << std::endl;
            }
        }
        files.clear();
    };

    // 使用 lock_guard 保证线程安全
    std::lock_guard<std::mutex> lock(g_results_mutex);
    if (category_mask & CATEGORY_TRASH) {
        // 回收站清理逻辑比较特殊，我们把它也整合进来
        if (home_dir_cstr) {
             uint64_t trash_size_before = calculate_directory_size(fs::path(home_dir_cstr) / ".local/share/Trash");
             // 调用内部的清空回收站逻辑
             internal_empty_trash(home_dir_cstr); // 假设 internal_empty_trash 存在
             total_freed_space += trash_size_before;
        }
        g_trash_files.clear();
    }
    if (category_mask & CATEGORY_PACKAGES) clear_file_list(g_package_files);
    if (category_mask & CATEGORY_COMPRESSED) clear_file_list(g_compressed_files);

    return total_freed_space;
}

//清理指定文件夹下的所有文件
API uint64_t CleanupDirectory(const char* dir_path_str) {
    // --- 1. 路径合法性检查 (初步) ---
    if (!dir_path_str || strlen(dir_path_str) == 0) {
        std::cerr << "[错误] 清理失败：提供的路径为空。" << std::endl;
        return 0;
    }

    fs::path dir_path(dir_path_str);
    std::error_code ec;

    // --- 2. 路径是否存在性检查 ---
    // fs::status 可以同时检查存在性和类型，比 fs::exists 更高效
    auto status = fs::status(dir_path, ec);
    if (!fs::exists(status)) {
        std::cerr << "[错误] 清理失败：路径 '" << dir_path_str << "' 不存在。" << std::endl;
        return 0;
    }

    // --- 路径合法性检查 (深入) ---
    // 检查是否是一个目录，而不是一个文件
    if (!fs::is_directory(status)) {
        std::cerr << "[错误] 清理失败：路径 '" << dir_path_str << "' 是一个文件，而不是一个目录。" << std::endl;
        return 0;
    }
    // 检查是否有读取权限，这是能进行下一步操作的基本前提
    // (注意：这里无法完美检查写入权限，但读取权限是一个很好的指标)
    if ((status.permissions() & fs::perms::owner_read) == fs::perms::none) {
         std::cerr << "[错误] 清理失败：没有读取路径 '" << dir_path_str << "' 的权限。" << std::endl;
        return 0;
    }

    // --- 3. 路径是否在家目录下的检查 (核心安全边界) ---
    const char* home_dir_cstr = getenv("HOME");
    if (!home_dir_cstr) {
        std::cerr << "[错误] 清理失败：无法获取用户主目录（HOME环境变量未设置）。" << std::endl;
        return 0;
    }
    std::string home_dir_str(home_dir_cstr);
    // 将路径规范化，以处理 ".." 或 "//" 等情况
    std::string canonical_path_str = fs::weakly_canonical(dir_path).string();

    // 检查规范化后的路径是否以主目录路径开头
    if (canonical_path_str.rfind(home_dir_str, 0) != 0) {
        std::cerr << "[错误] 清理失败：路径 '" << dir_path_str 
                  << "' 不在用户主目录下，需要管理员权限（sudo），操作被拒绝以确保安全。" << std::endl;
        return 0;
    }

    // --- 通过所有检查，开始执行清理 ---
    std::cout << "[信息] 路径 '" << dir_path_str << "' 通过所有安全检查，开始清理其下的所有文件..." << std::endl;
    uint64_t total_freed_space = 0;
    
    try {
	  // --- 阶段一：删除所有文件 ---
        // --- 核心修改：使用递归迭代器遍历所有子孙文件 ---
        for (const auto& entry : fs::recursive_directory_iterator(dir_path, fs::directory_options::skip_permission_denied)) {
            // --- 逐个文件进行删除，并进行精细的错误处理 ---
            // 确保我们只处理文件，跳过目录
            if (entry.is_regular_file()) {
                std::error_code ec_size, ec_remove;
                uint64_t file_size = fs::file_size(entry, ec_size);

                if (!ec_size) {
                    fs::remove(entry.path(), ec_remove);
                    if (!ec_remove) {
                        // 只有在获取大小和删除都成功时，才增加计数
                        total_freed_space += file_size;
                    } else {
                        // 如果删除失败，打印具体错误并继续
                        std::cerr << "[警告] 无法删除文件 '" << entry.path().string() 
                                  << "': " << ec_remove.message() << std::endl;
                    }
                } else {
                    std::cerr << "[警告] 无法获取文件大小 '" << entry.path().string() 
                              << "': " << ec_size.message() << std::endl;
                }
            }
        }
    } catch (const std::exception& e) {
        // 捕获迭代器本身可能产生的错误
        std::cerr << "[错误] 清理过程中遍历目录时发生错误: " << e.what() << std::endl;
    }

    // --- 阶段二：安全地清理所有产生的空目录 ---
    try {
        // 1. 收集所有子目录的路径
        std::vector<fs::path> subdirs;
        for (const auto& entry : fs::recursive_directory_iterator(dir_path, fs::directory_options::skip_permission_denied)) {
            if (entry.is_directory()) {
                subdirs.push_back(entry.path());
            }
        }

        // 2. 按路径长度降序排序 (从最深的目录开始处理)
        std::sort(subdirs.begin(), subdirs.end(), [](const fs::path& a, const fs::path& b) {
            return a.string().length() > b.string().length();
        });

        // 3. 遍历收集到的路径列表并删除空目录
        for (const auto& subdir_path : subdirs) {
            // 再次检查目录是否存在且为空
            if (fs::exists(subdir_path) && fs::is_empty(subdir_path)) {
                 // 确保不删除用户传入的根目录本身
                if (subdir_path != dir_path) {
                    fs::remove(subdir_path);
                }
            }
        }
    } catch (const std::exception& e) {
         std::cerr << "[警告] 清理空目录时发生错误: " << e.what() << std::endl;
    }

    return total_freed_space;
}

//搬迁指定文件类型 
int MigrateCategories(unsigned int category_mask, const char* destination_dir) {
    fs::path dest(destination_dir);
    try {
        if (!fs::exists(dest)) {
            fs::create_directories(dest);
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to create destination directory: " << e.what() << std::endl;
        return -1;
    }
    
    // 辅助lambda，用于搬迁文件列表
    auto migrate_list = [&](std::vector<FileInfo>& files) {
        for (const auto& file_info : files) {
            try {
                fs::path source(file_info.path);
                if (fs::exists(source)) {
                    fs::rename(source, dest / source.filename());
                }
            } catch (const fs::filesystem_error& e) {
                std::cerr << "Failed to move " << file_info.path << ": " << e.what() << std::endl;
                // continue on error
            }
        }
        files.clear();
    };
    
    std::lock_guard<std::mutex> lock(g_results_mutex);
    if (category_mask & CATEGORY_VIDEO) migrate_list(g_video_files);
    if (category_mask & CATEGORY_AUDIO) migrate_list(g_audio_files);
    if (category_mask & CATEGORY_IMAGE) migrate_list(g_image_files);
    if (category_mask & CATEGORY_DOCUMENT) migrate_list(g_document_files);

    return 0;
}
// --- 新增 API 的实现 (修复崩溃的关键) ---
void CleanupScanner() {
    if (g_scan_thread.joinable()) {
        g_scan_thread.join();
    }
}