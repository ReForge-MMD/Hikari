use std::env;
use std::path::{Path, PathBuf};

const JOLT: &str = "deps/jolt";
const BRIDGE: &str = "cpp/mmd_physics.cpp";

const SKIP_DIRS: &[&str] = &[
    "Compute/CPU",
    "Compute/DX12",
    "Compute/MTL",
    "Compute/VK",
    "Renderer",
    "Shaders",
    "Physics/Hair",
];

fn main() {
    let root = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let target = env::var("TARGET").unwrap();

    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed=cpp");
    println!("cargo:rerun-if-changed={JOLT}");

    let jolt_root = root.join(JOLT).join("Jolt");
    let mut sources = Vec::new();
    collect(&jolt_root, &jolt_root, &mut sources);
    sources.sort();
    sources.push(root.join(BRIDGE));

    let mut build = cc::Build::new();
    build
        .cpp(true)
        .std("c++17")
        .include(root.join(JOLT))
        .include(root.join("cpp"))
        .files(&sources)
        .warnings(false)
        .define("JPH_CROSS_PLATFORM_DETERMINISTIC", None)
        .define("JPH_OBJECT_LAYER_BITS", "16")
        .opt_level(3)
        .debug(false)
        .define("NDEBUG", None);

    if target.contains("apple") {
        build.flag("-mmacosx-version-min=11.0");
    }

    build.compile("mmd_physics");

    if target.contains("apple") {
        println!("cargo:rustc-link-lib=c++");
    } else if target.contains("windows") {
        // MSVC links its C++ runtime automatically.
    } else {
        println!("cargo:rustc-link-lib=stdc++");
    }
}

fn collect(dir: &Path, root: &Path, out: &mut Vec<PathBuf>) {
    let read_dir = match std::fs::read_dir(dir) {
        Ok(rd) => rd,
        Err(_) => return,
    };
    for entry in read_dir {
        let entry = match entry {
            Ok(e) => e,
            Err(_) => continue,
        };
        let path = entry.path();
        let rel = match path.strip_prefix(root) {
            Ok(r) => r.to_string_lossy().replace('\\', "/"),
            Err(_) => continue,
        };
        if path.is_dir() {
            if !SKIP_DIRS.iter().any(|skip| rel == *skip) {
                collect(&path, root, out);
            }
        } else if path.extension().is_some_and(|e| e == "cpp") {
            out.push(path);
        }
    }
}
