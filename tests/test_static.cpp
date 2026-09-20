#include "handlers.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace {

// Builds a throwaway static root so the tests do not depend on repo contents.
class StaticFiles : public ::testing::Test {
protected:
    void SetUp() override {
        root = std::filesystem::temp_directory_path() / "server_static_test";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "sub");

        write(root / "index.html", "<h1>root</h1>");
        write(root / "style.css", "body{}");
        write(root / "empty.txt", "");
        write(root / "sub" / "index.html", "<h1>sub</h1>");
        write(root / "noext", "raw");

        // A file the server must never serve: it sits outside the static root.
        write(std::filesystem::temp_directory_path() / "server_secret.txt", "TOP SECRET");
    }

    void TearDown() override {
        std::filesystem::remove_all(root);
        std::filesystem::remove(std::filesystem::temp_directory_path() / "server_secret.txt");
    }

    static void write(const std::filesystem::path& path, const std::string& content) {
        std::ofstream file(path, std::ios::binary);
        file << content;
    }

    HttpResponse fetch(const std::string& path) {
        StaticFileHandler handler(root.string(), 8);
        HttpRequest request;
        request.method = "GET";
        request.path = path;
        return handler(request);
    }

    std::filesystem::path root;
};

}  // namespace

TEST_F(StaticFiles, ServesFile) {
    const HttpResponse response = fetch("/index.html");
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "<h1>root</h1>");
    EXPECT_EQ(response.contentType, "text/html");
}

TEST_F(StaticFiles, RootServesIndex) {
    EXPECT_EQ(fetch("/").body, "<h1>root</h1>");
}

TEST_F(StaticFiles, DirectoryWithSlashServesItsIndex) {
    EXPECT_EQ(fetch("/sub/").body, "<h1>sub</h1>");
}

TEST_F(StaticFiles, MissingFileIs404) {
    EXPECT_EQ(fetch("/nope.html").status, 404);
}

TEST_F(StaticFiles, DirectoryWithoutSlashIs404) {
    EXPECT_EQ(fetch("/sub").status, 404);
}

TEST_F(StaticFiles, EmptyFileIs200NotMissing) {
    const HttpResponse response = fetch("/empty.txt");
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "");
}

TEST_F(StaticFiles, DetectsContentTypes) {
    EXPECT_EQ(fetch("/style.css").contentType, "text/css");
    EXPECT_EQ(fetch("/noext").contentType, "application/octet-stream");
}

// The security property: no request target may escape the static root, however
// it is spelled.
TEST_F(StaticFiles, RejectsPathTraversal) {
    for (const std::string attack : {
             "/../server_secret.txt",
             "/../../etc/passwd",
             "/sub/../../server_secret.txt",
             "/./../server_secret.txt"}) {
        const HttpResponse response = fetch(attack);
        EXPECT_EQ(response.status, 403) << attack;
        EXPECT_EQ(response.body.find("SECRET"), std::string::npos) << attack;
    }
}

TEST_F(StaticFiles, TraversalInsideRootIsAllowed) {
    // Resolves back inside the root, so it is a normal 200.
    EXPECT_EQ(fetch("/sub/../index.html").status, 200);
}

TEST_F(StaticFiles, RejectsNonAbsolutePath) {
    EXPECT_EQ(fetch("index.html").status, 403);
}

TEST_F(StaticFiles, SecondReadComesFromCache) {
    StaticFileHandler handler(root.string(), 8);
    HttpRequest request;
    request.method = "GET";
    request.path = "/index.html";

    handler(request);
    handler(request);

    EXPECT_EQ(handler.files().cacheHits(), 1u);
    EXPECT_EQ(handler.files().cacheMisses(), 1u);
}
