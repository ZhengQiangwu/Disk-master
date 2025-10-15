// test_app/main.cpp (完全重写)
#include "disk_cleaner.h"
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>    // 文件流操作
#include <iomanip> // 用于设置输出精度
#include <sstream> // 用于字符串格式化
namespace fs = std::filesystem;

// --- 新增：一个友好的文件大小格式化函数 ---
std::string format_size(uint64_t bytes) {
    if (bytes == 0) return "0 KB";

    std::stringstream ss;
    ss << std::fixed << std::setprecision(1); // 设置所有浮点数输出都保留一位小数

    double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0) {
        ss << kb << " KB";
        return ss.str();
    }

    double mb = kb / 1024.0;
    if (mb < 1024.0) {
        ss << mb << " MB";
        return ss.str();
    }

    double gb = mb / 1024.0;
    ss << gb << " GB";
    return ss.str();
}

// 回调函数保持不变
void my_scan_callback(const char* file_path, uint64_t file_size, uint64_t total_scanned_size, FileCategory category) {
    // 可以在这里加一个开关，决定是否实时打印扫描细节
    // std::cout << "发现文件 -> 路径: " << file_path << "\n";
    std::cout << "发现文件 -> 路径: " << file_path
              << ", 大小: " << file_size / 1024 << " KB"
              << ", 当前垃圾总计: " << total_scanned_size / (1024 * 1024) << " MB\n";
}

// 辅助函数，用于显示每个分类的结果
// 更新 display_category_results 以使用新的枚举
void display_category_results(FileCategory category, const std::string& title) {
    int count = 0;
    // 注意：get_scan_results 仍然使用单个枚举值
    FileInfo* files = GetScanResults(static_cast<FileCategory>(category), &count); 
    
    if (files && count > 0) {
        uint64_t total_size = 0;
        for (int i = 0; i < count; ++i) {
            total_size += files[i].size;
        }
        std::cout << "  - " << title << " (" << count << " 个): " << format_size(total_size) << std::endl;
        FreeScanResults(files, count);
    } else {
        std::cout << "  - " << title << ": \t\t0 KB" << std::endl;
    }
}

int main() {
    const char* home_dir = getenv("HOME");
    if (!home_dir) {
        std::cerr << "无法获取主目录路径！" << std::endl;
        return 1;
    }

    std::cout << "正在扫描 " << home_dir << "，请稍候...\n";
    StartScan(home_dir, my_scan_callback);
    
    // 主线程等待0.5秒
    //usleep(300000);

    // 在扫描仍在进行时，从主线程调用 stop_scan
    //std::cout << "\n>>> 主线程：发送停止扫描请求...\n";
    //StopScan();

    while (IsScanFinished() == 0) {
        std::cout << ".";
        std::cout.flush(); // 确保点号能立即显示
        sleep(1);
    }
    std::cout << "\n扫描完成！\n" << std::endl;

    // --- 显示扫描结果，按新分类 ---
    std::cout << "--- 垃圾清理 (可删除) ---\n";
    display_category_results(CATEGORY_PACKAGES,   "软件安装包");
    display_category_results(CATEGORY_COMPRESSED, "压缩包文件");
    // 使用新API获取准确、不重叠的缓存大小
    // --- 核心修改：使用新方法获取所有特殊项的大小 ---
    uint64_t trash_size = GetSpecialCategorySize(CATEGORY_TRASH);
    uint64_t other_cache_size = GetSpecialCategorySize(CATEGORY_OTHER_APP_CACHE);
    uint64_t thumb_cache_size = GetSpecialCategorySize(CATEGORY_THUMBNAIL_CACHE);
    std::cout << "  - 回收站: \t\t" << format_size(trash_size) << std::endl;
    std::cout << "  - 其他用户应用缓存: \t" << format_size(other_cache_size) << std::endl;
    std::cout << "  - 缩略图缓存: \t" << format_size(thumb_cache_size) << std::endl;
    std::cout << std::endl;

    std::cout << "--- 大文件搬迁 (可移动) ---\n";
    display_category_results(CATEGORY_VIDEO,      "视频文件");
    display_category_results(CATEGORY_AUDIO,      "音频文件");
    display_category_results(CATEGORY_IMAGE,      "图片文件");
    display_category_results(CATEGORY_DOCUMENT,   "文档文件");
    std::cout << std::endl;

    // --- 在所有操作之前，创建测试环境 ---
    fs::path move_files_dir = fs::path(home_dir) / "MoveFiles";
    fs::create_directory(move_files_dir);
    // 在排除目录里创建一个假的视频文件
    std::ofstream excluded_video(move_files_dir / "already_moved.mp4");
    excluded_video.close();
    // 在非排除目录里创建一个视频文件
    std::ofstream normal_video(fs::path(home_dir) / "new_video.mp4");
    normal_video.close();

    std::cout << "[测试准备] 已在 'MoveFiles' 目录和主目录下各创建一个测试视频文件。\n" << std::endl;
    
    // ... (执行正常的扫描流程) ...
    StartScan(home_dir, my_scan_callback);
    // ...
    sleep(1);
    // --- 在显示结果时，用户可以观察到 '视频文件' 的数量 ---
    std::cout << "--- 大文件搬迁 (可移动) ---\n";
    // 预期结果：视频文件数量应该是 1，而不是 2
    display_category_results(CATEGORY_VIDEO,      "视频文件");

//---------------------------------------------------------------------
/*
    // --- 演示自定义文件类型 ---
    std::cout << "\n>>> 演示: 自定义文件类型...\n";
    
    // 1. 创建测试 .log 文件
    //fs::path test_log_file = fs::path(home_dir) / "my_app.pdf";
    //std::ofstream(test_log_file).close();
    //std::cout << "  创建了测试文件: " << test_log_file.string() << std::endl;
    
    // 2. 定义新的文档扩展名列表
    const char* new_doc_exts[] = { ".pdf" };
    std::cout << "  将 '文档' 类别重新定义为只包含.md 文件。\n";
    SetExtensions(CATEGORY_DOCUMENT, new_doc_exts, 1);

    // 3. 重新扫描以应用新规则
    std::cout << "  正在使用新规则重新扫描...\n";
    StartScan(home_dir, my_scan_callback);
    while (IsScanFinished() == 0) {  }
    std::cout << "  重新扫描完成！\n";
    
    // 4. 显示新结果
    std::cout << "--- 自定义规则下的扫描结果 ---\n";
    display_category_results(CATEGORY_DOCUMENT, "文档文件");
    std::cout << "  (注意：自定义规则将在此次程序运行中持续有效)\n";
*/
//---------------------------------------------------------------------
/*
    fs::path test_dir = fs::path(home_dir) / "TestCleanupDir";
    uint64_t freed_in_dir = CleanupDirectory(test_dir.c_str());
    std::cout << "  调用 cleanup_directory 完成，清理大小: " << format_size(freed_in_dir) << "\n";
*/
/*
//---------------------------------------------------------------------
    // --- 演示清理指定文件夹 ---
    std::cout << ">>> 演示: 清理指定文件夹 (只删文件，保留目录)...\n";

    fs::path test_dir = fs::path(home_dir) / "TestCleanupDir";
    fs::path sub_dir = test_dir / "sub";
    fs::create_directories(sub_dir); // 创建测试目录和子目录

    // 创建几个测试文件
    std::ofstream(test_dir / "dummy.zip").close();
    std::ofstream(sub_dir / "nested_dummy.tar.gz").close();
    
    std::cout << "  在 '" << test_dir << "' 及其子目录下创建了测试文件和目录。\n";
    std::cout << "  清理前，目录 '" << sub_dir << "' 存在: " << (fs::exists(sub_dir) ? "是" : "否") << std::endl;

    uint64_t freed_in_dir = CleanupDirectory(test_dir.c_str());
    
    std::cout << "  调用 cleanup_directory 完成，清理大小: " << format_size(freed_in_dir) << "\n";
    std::cout << "  清理后，顶层目录 '" << test_dir << "' 存在: " << (fs::exists(test_dir) ? "是" : "否") << std::endl;
    // 因为子目录下的文件被删了，子目录变空，所以它应该被删除了
    std::cout << "  清理后，空的子目录 '" << sub_dir << "' 存在: " << (fs::exists(sub_dir) ? "是" : "否") << std::endl;

    //fs::remove_all(test_dir); // 最后清理测试目录
    //std::cout << "  测试目录已删除。\n" << std::endl;
	
    // 测试用例 2: 失败 - 路径不存在
    std::cout << "\n--- 测试用例 2: 不存在的路径 ---\n";
    CleanupDirectory("/path/to/nonexistent/dir");

    // 测试用例 3: 失败 - 路径在主目录外 (模拟需要sudo)
    std::cout << "\n--- 测试用例 3: 主目录外的路径 ---\n";
    CleanupDirectory("/tmp"); // /tmp 是一个常见且安全的测试目标
    std::cout << "\n>>> 演示结束 <<<\n" << std::endl;*/

/*
    // --- 演示选择性操作 ---
    
    // 演示1: 清理回收站和安装包
    std::cout << ">>> 演示: 清理 [回收站]...\n";
    unsigned int cleanup_mask = CATEGORY_TRASH;//清理回收站
    //unsigned int cleanup_mask = CATEGORY_TRASH | CATEGORY_PACKAGES;//清理回收站和安装包
    uint64_t freed_space = CleanupCategories(cleanup_mask);
    std::cout << ">>> 清理完成，共释放空间: " << format_size(freed_space) << "\n" << std::endl;

    // 演示2: 搬迁视频和图片文件
    std::cout << ">>> 演示: 搬迁 [视频文件]...\n";
    std::string dest_path = std::string(home_dir) + "/MigratedFiles";
    unsigned int migrate_mask = CATEGORY_VIDEO;//搬迁视频
    //unsigned int migrate_mask = CATEGORY_VIDEO | CATEGORY_IMAGE;
    int result = MigrateCategories(migrate_mask, dest_path.c_str());
    if (result == 0) {
        std::cout << ">>> 搬迁成功！文件已移动到 " << dest_path << "\n" << std::endl;
    } else {
        std::cout << ">>> 搬迁失败。\n" << std::endl;
    }
    // 演示3: 也可以全选清理
    // cleanup_categories(CATEGORY_ALL_CLEANUP);*/
    
    CleanupScanner();
    std::cout << "测试程序执行完毕。" << std::endl;
    return 0;
}