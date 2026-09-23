#include <chromerelay/runtime.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace chromerelay;
namespace fs = std::filesystem;
unsigned passed = 0, failed = 0;
std::string step;
void check(bool condition, const char *name) {
  if (condition)
    ++passed;
  else {
    ++failed;
    std::cerr << "FAIL: " << name << '\n';
  }
}
template <class F> void rejects(F action, const char *name) {
  try {
    action();
    check(false, name);
  } catch (const std::exception &) {
    check(true, name);
  }
}
void write(const fs::path &path, const std::string &value) {
  std::ofstream stream(path, std::ios::binary);
  stream.write(value.data(), static_cast<std::streamsize>(value.size()));
  if (!stream)
    throw RelayError("Fixture write failed");
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  auto pattern =
      (fs::temp_directory_path() / "chromerelay-files-XXXXXX").string();
  if (!::mkdtemp(pattern.data()))
    return 2;
  const auto base = fs::canonical(pattern), allowed = base / "allowed",
             outside = base / "outside";
  struct Cleanup {
    fs::path path;
    ~Cleanup() {
      std::error_code error;
      fs::remove_all(path, error);
    }
  } cleanup{base};
  try {
    const auto *site = std::getenv("CHROMERELAY_FIXTURE_URL"),
               *artifact = std::getenv("CHROMERELAY_CHILD_EVIDENCE");
    if (!site || !artifact)
      throw RelayError("Missing fixture environment");
    const fs::path evidence = fs::absolute(artifact);
    fs::create_directories(evidence);
    fs::create_directory(allowed);
    fs::create_directory(outside);
    const auto first = allowed / "资料😀.txt", second = allowed / "bytes.bin";
    write(first, "Browser file 中文😀\n");
    write(second, std::string("\0\1\xff"
                              "A",
                              4));
    write(outside / "sentinel.txt", "untouched");
    ActionRuntime runtime(static_cast<unsigned>(std::stoul(argv[1])),
                          {allowed, evidence});
    ActionCatalog catalog;
    auto call = [&](const std::string &name, Json arguments = Json::object()) {
      step = name + " " + arguments.dump();
      return runtime.invoke(catalog.resolve(name, arguments, true));
    };
    auto eval = [&](const std::string &expression) {
      return call("page_evaluate", {{"script", expression}}).at("result");
    };
    call("tab_create");
    call("page_navigate", {{"url", std::string(site) + "/page.html"}});
    eval("window.fileProof=[];document.querySelector('#files')."
         "addEventListener('change',e=>fileProof.push({trusted:e.isTrusted,"
         "count:e.target.files.length}));true");
    check(
        call("form_upload", {{"selector", "#files"}, {"files", first.string()}})
                .at("uploaded") == 1,
        "single string upload accepted");
    check(eval("document.querySelector('#files').files[0].name") ==
              "资料😀.txt",
          "native upload preserves Unicode basename");
    check(eval("document.querySelector('#files').files[0].text()") ==
              "Browser file 中文😀\n",
          "browser reads actual uploaded file bytes");
    check(eval("fileProof.at(-1).trusted") == true,
          "upload change event comes from browser");
    call("upload_file",
         {{"selector", "#files"},
          {"files", Json::array({first.string(), second.string()})}});
    check(eval("document.querySelector('#files').files.length") == 2,
          "legacy upload selects multiple files");
    check(eval("document.querySelector('#files').files[1].arrayBuffer().then(b="
               ">[...new Uint8Array(b)])") == Json::array({0, 1, 255, 65}),
          "binary file bytes survive native selection");
    rejects(
        [&] {
          call("form_upload",
               {{"selector", "#files"},
                {"files", Json::array({first.string(),
                                       (outside / "sentinel.txt").string()})}});
        },
        "mixed authorized and outside batch rejected");
    check(eval("document.querySelector('#files').files.length") == 2,
          "batch validation failure preserves previous selection");
    Json too_many = Json::array();
    for (int index = 0; index < 129; ++index)
      too_many.push_back(first.string());
    rejects(
        [&] {
          call("form_upload", {{"selector", "#files"}, {"files", too_many}});
        },
        "upload batch count bounded before browser mutation");
    rejects(
        [&] {
          call("form_upload", {{"selector", "#files"},
                               {"files", (allowed / "missing").string()}});
        },
        "missing file rejected before browser update");
    fs::create_symlink(outside / "sentinel.txt", allowed / "escape");
    rejects(
        [&] {
          call("form_upload", {{"selector", "#files"},
                               {"files", (allowed / "escape").string()}});
        },
        "upload outside symlink refused");
    fs::create_symlink(first, allowed / "inside");
    call("form_upload",
         {{"selector", "#files"}, {"files", (allowed / "inside").string()}});
    check(eval("document.querySelector('#files').files[0].text()") ==
              "Browser file 中文😀\n",
          "allowed symlink uses canonical source file");
    rejects(
        [&] {
          call("form_upload",
               {{"selector", "#person"}, {"files", first.string()}});
        },
        "non-file input rejected");
    eval("document.body.insertAdjacentHTML('beforeend','<input id=single "
         "type=file>');true");
    rejects(
        [&] {
          call("form_upload",
               {{"selector", "#single"},
                {"files", Json::array({first.string(), second.string()})}});
        },
        "multiple files refused by single-file control");
    eval("document.querySelector('#files').style.display='none';true");
    call("form_upload", {{"selector", "#files"}, {"files", second.string()}});
    check(eval("document.querySelector('#files').files[0].size") == 4,
          "hidden file input receives native selection");
    check(
        call("form_upload", {{"selector", "#files"}, {"files", Json::array()}})
                .at("uploaded") == 0,
        "empty file array clears native selection");
    check(eval("document.querySelector('#files').files.length") == 0,
          "file clearing visible in browser");
    fs::create_directories(allowed / "folder/nested");
    write(allowed / "folder/a.txt", "alpha");
    write(allowed / "folder/nested/b.txt", "beta");
    eval("document.body.insertAdjacentHTML('beforeend','<input id=directory "
         "type=file webkitdirectory>');true");
    call("form_upload", {{"selector", "#directory"},
                         {"files", (allowed / "folder").string()}});
    check(eval("[...document.querySelector('#directory').files].map(f=>f."
               "webkitRelativePath).sort()") ==
              Json::array({"folder/a.txt", "folder/nested/b.txt"}),
          "directory upload preserves nested relative paths");
    check(eval("Promise.all([...document.querySelector('#directory').files]."
               "map(f=>f.text())).then(values=>values.sort())") ==
              Json::array({"alpha", "beta"}),
          "browser reads actual directory member bytes");
    call("form_upload", {{"selector", "#directory"},
                         {"files", (allowed / "folder").string()}});
    check(eval("document.querySelector('#directory').files.length") == 2,
          "reselecting same directory completes");
    fs::create_symlink(outside / "sentinel.txt", allowed / "folder/escape");
    rejects(
        [&] {
          call("form_upload", {{"selector", "#directory"},
                               {"files", (allowed / "folder").string()}});
        },
        "directory upload rejects symlink members before browser selection");
    check(eval("document.querySelector('#directory').files.length") == 2,
          "rejected directory preserves earlier selection");
    call("page_navigate", {{"url", std::string(site) + "/capture.html"}});
    runtime.browser().page_call("Emulation.setDeviceMetricsOverride",
                                {{"width", 800},
                                 {"height", 600},
                                 {"deviceScaleFactor", 1},
                                 {"mobile", false}});
    auto save = [&](const std::string &name, const Json &result) {
      if (result.contains("screenshot")) {
        auto image = decode_png(result.at("screenshot"));
        PathAccess({evidence})
            .output((evidence / name).string())
            .commit(image.bytes);
      }
      auto metadata = result;
      metadata.erase("screenshot");
      write(evidence / (name + ".json"), metadata.dump(2));
    };
    const auto viewport = call("page_capture");
    save("viewport.png", viewport);
    check(viewport.at("width") == 800 && viewport.at("height") == 600,
          "viewport PNG has expected pixel dimensions");
    check(viewport.at("screenshot").get<std::string>().size() > 1000,
          "inline screenshot contains complete Base64 rather than a preview");
    const auto full = call("page_capture", {{"fullPage", true}});
    save("full.png", full);
    check(full.at("width") == 800 && full.at("height") == 1600,
          "full-page PNG includes entire document height");
    const auto element = call("page_capture", {{"selector", "#swatch"}});
    save("element.png", element);
    check(element.at("width") == 200 && element.at("height") == 120,
          "element capture crops exact visible box");
    eval("scrollTo(0,700);true");
    const auto scrolled = call("page_capture");
    save("scrolled.png", scrolled);
    check(scrolled.at("width") == 800 && scrolled.at("height") == 600,
          "scrolled viewport capture remains viewport sized");
    const auto far = call("page_capture", {{"selector", "#far"}});
    save("far.png", far);
    check(far.at("width") == 160 && far.at("height") == 90,
          "offscreen element scrolled and captured");
    const auto saved =
        call("screenshot", {{"selector", "#swatch"},
                            {"path", (evidence / "saved.png").string()}});
    save("saved.png", saved);
    check(saved.at("saved") == fs::canonical(evidence / "saved.png").string() &&
              !saved.contains("screenshot"),
          "legacy screenshot saves complete file and returns canonical "
          "destination");
    check(fs::file_size(evidence / "saved.png") == saved.at("size"),
          "saved file byte count matches result");
    rejects(
        [&] {
          call("page_capture", {{"path", (outside / "blocked.png").string()}});
        },
        "screenshot outside configured roots refused");
    check(!fs::exists(outside / "blocked.png"),
          "refused screenshot creates no outside file");
    call("browser_configure", {{"long_timeout", 150}});
    rejects(
        [&] {
          call("page_capture", {{"selector", "#hidden-never"},
                                {"path", (evidence / "failed.png").string()}});
        },
        "missing screenshot selector fails with bounded wait");
    call("browser_configure", {{"long_timeout", 30000}});
    check(!fs::exists(evidence / "failed.png"),
          "failed capture never leaves incomplete destination");
    write(allowed / "preserved.png", "original image placeholder");
    call("browser_configure", {{"long_timeout", 150}});
    rejects(
        [&] {
          call("page_capture",
               {{"selector", "#hidden-never"},
                {"path", (allowed / "preserved.png").string()}});
        },
        "failed capture does not commit over existing file");
    call("browser_configure", {{"long_timeout", 30000}});
    check(read_text(allowed / "preserved.png") == "original image placeholder",
          "existing destination survives capture failure");
    runtime.browser().page_call("Emulation.setDeviceMetricsOverride",
                                {{"width", 800},
                                 {"height", 600},
                                 {"deviceScaleFactor", 2},
                                 {"mobile", false}});
    eval("scrollTo(0,0);true");
    const auto retina = call("page_capture");
    save("retina.png", retina);
    check(retina.at("width") == 1600 && retina.at("height") == 1200,
          "device scale reflected in physical screenshot pixels");
    runtime.browser().page_call("Emulation.setDeviceMetricsOverride",
                                {{"width", 800},
                                 {"height", 600},
                                 {"deviceScaleFactor", 8},
                                 {"mobile", false}});
    rejects([&] { call("page_capture", {{"fullPage", true}}); },
            "oversized physical capture rejected before browser rasterization");
    runtime.browser().page_call("Emulation.clearDeviceMetricsOverride");
    call("page_navigate", {{"url", std::string(site) + "/frame-host.html"}});
    call("page_wait", {{"type", "function"},
                       {"expression", "readyFrames.length>=6"},
                       {"timeout", 5000}});
    call("frame_enter", {{"selector", "#crossFrame"}});
    eval("document.body.innerHTML='<input id=files type=file><div id=panel "
         "style=\"width:160px;height:90px;background:rgb(30,80,220)\"></"
         "div>';true");
    call("form_upload", {{"selector", "#files"}, {"files", first.string()}});
    check(eval("document.querySelector('#files').files[0].text()") ==
              "Browser file 中文😀\n",
          "native file selection works in OOP frame");
    const auto framed = call("page_capture", {{"selector", "#panel"}});
    save("oop-element.png", framed);
    check(framed.at("width").get<unsigned>() > 100 &&
              framed.at("height").get<unsigned>() > 60,
          "OOP element capture uses root projected bounds");
    call("frame_reset");
    check(eval("document.querySelector('#files')===null") == true,
          "frame upload does not create input in parent");
    call("tab_close");
    std::cout << passed << " live file/capture checks passed; " << failed
              << " failed\n";
    return failed ? 1 : 0;
  } catch (const std::exception &error) {
    std::cerr << step << "\n" << error.what() << '\n';
    return 1;
  }
}
