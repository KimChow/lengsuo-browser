// Standalone Rust TEST encoder for the LBPF1 wire format (schemas 1 and 2).
// Independent reimplementation: built from the explicit fake field
// dictionary below, NOT by reading or patching golden fixtures.
// This encoder does NOT update the client production ProfileSpecV1 API;
// it exists only to cross-verify the C++/Python encoders byte for byte.
use std::path::PathBuf;

// ---- Fake golden dictionary (single source of truth for v1 and v2) ----
const MAGIC: &[u8; 5] = b"LBPF1";
const SCHEMA_V1: u32 = 1;
const SCHEMA_V2: u32 = 2;
const NOISE_VERSION: u32 = 1;
const ENVIRONMENT_ID: &str = "env-1";
const POOL_VERSION: &str = "p1";
const NOISE_SEED: [u8; 8] = [0, 1, 2, 3, 4, 5, 6, 7];
const PLATFORM_WINDOWS: u8 = 0; // matches native/profile_wire.h enum
const WIDTH: u32 = 1920;
const HEIGHT: u32 = 1080;
const COLOR_DEPTH: u8 = 24;
const DPR_MILLI: u32 = 1000;
const CPU_CORES: u16 = 8;
const MEMORY_MIB: u32 = 8192;
const MAX_TOUCH_POINTS: u8 = 0;
const LANGUAGE: &str = "en-US";
const TIMEZONE: &str = "UTC";
const WEBRTC_POLICY: u8 = 2; // schema 2 only; 0..3 per design proposal
const SLOT_COUNT: u16 = 7;
// Canonical face order and slot ids from the dictionary.
const SLOTS: [(&str, u32); 7] = [
    ("audio", 3),
    ("canvas", 1),
    ("client_rects", 5),
    ("fonts", 4),
    ("gpu", 6),
    ("ua", 7),
    ("webgl", 2),
];

// ---- Primitive writers (big-endian, mirroring WireReader semantics) ----
fn num(out: &mut Vec<u8>, value: u64, count: usize) {
    assert!((1..=4).contains(&count), "unsupported number width");
    assert!(
        value < (1u64 << (8 * count)),
        "value does not fit in {count} bytes"
    );
    for i in (0..count).rev() {
        out.push(((value >> (8 * i)) & 0xff) as u8);
    }
}

fn text(out: &mut Vec<u8>, s: &str, identifier: bool) {
    let bytes = s.as_bytes();
    assert!(!bytes.is_empty(), "Text() rejects empty values");
    assert!(bytes.len() <= u16::MAX as usize, "text too long");
    // Printable check (WireReader::OptionalText rejects c < 32 || c > 126).
    assert!(
        bytes.iter().all(|&c| c >= 32 && c <= 126),
        "unprintable text"
    );
    if identifier {
        assert!(
            bytes
                .iter()
                .all(|&c| { c.is_ascii_alphanumeric() || c == b'-' || c == b'_' }),
            "non-identifier character in identifier text"
        );
    }
    num(out, bytes.len() as u64, 2);
    out.extend_from_slice(bytes);
}

// ---- Field-order encoder, schema-gated ----
fn encode(schema: u32) -> Vec<u8> {
    assert!(schema == SCHEMA_V1 || schema == SCHEMA_V2);
    let mut out = Vec::new();
    out.extend_from_slice(MAGIC);
    num(&mut out, schema as u64, 4);
    num(&mut out, NOISE_VERSION as u64, 4);
    text(&mut out, ENVIRONMENT_ID, true);
    text(&mut out, POOL_VERSION, true);
    for b in NOISE_SEED {
        num(&mut out, b as u64, 1);
    }
    num(&mut out, PLATFORM_WINDOWS as u64, 1);
    num(&mut out, WIDTH as u64, 4);
    num(&mut out, HEIGHT as u64, 4);
    num(&mut out, COLOR_DEPTH as u64, 1);
    num(&mut out, DPR_MILLI as u64, 4);
    num(&mut out, CPU_CORES as u64, 2);
    num(&mut out, MEMORY_MIB as u64, 4);
    num(&mut out, MAX_TOUCH_POINTS as u64, 1);
    text(&mut out, LANGUAGE, false);
    text(&mut out, TIMEZONE, false);
    if schema == SCHEMA_V2 {
        // Proposed policy byte: after timezone, before slot_count.
        assert!(WEBRTC_POLICY <= 3, "policy outside 0..3 domain");
        num(&mut out, WEBRTC_POLICY as u64, 1);
    }
    num(&mut out, SLOT_COUNT as u64, 2);
    for (name, slot) in SLOTS {
        text(&mut out, name, true);
        num(&mut out, slot as u64, 4);
    }
    out
}

fn first_diff(a: &[u8], b: &[u8]) -> Option<usize> {
    a.iter().zip(b.iter()).position(|(x, y)| x != y)
}

fn golden_fixture(path: &PathBuf) -> Vec<u8> {
    // Parse a `constexpr unsigned char k...[] = {..};` C++ array. Used only
    // for COMPARING published fixtures against independently encoded bytes;
    // the encoder above never reads these files.
    let body = std::fs::read_to_string(path)
        .unwrap_or_else(|e| panic!("cannot read {}: {e}", path.display()));
    let open = body.find('{').expect("no '{' in golden.inc");
    let close = body.rfind('}').expect("no '}' in golden.inc");
    let inner = &body[open + 1..close];
    inner
        .split(',')
        .map(|t| t.trim())
        .filter(|t| !t.is_empty())
        .map(|t| {
            t.parse::<u8>()
                .unwrap_or_else(|e| panic!("bad byte '{t}' in {}: {e}", path.display()))
        })
        .collect()
}

fn reviews_dir() -> PathBuf {
    let dir = PathBuf::from(env!("REVIEWS_DIR"));
    std::fs::create_dir_all(&dir).expect("create reviews dir");
    dir
}

// ---- Tests ----
// Offset where the fixed-size scalar block ends and the timezone text
// field begins: magic(5)+schema(4)+noise(4)+env(tl=5+6)+pool(tl=2+3)+
// seed(8)+platform(1)+w(4)+h(4)+depth(1)+dpr(4)+cpu(2)+mem(4)+touch(1)
// +lang(tl=3+5) = 55 for this dictionary.
fn tz_start() -> usize {
    5 + 4
        + 4
        + (2 + ENVIRONMENT_ID.len())
        + (2 + POOL_VERSION.len())
        + 8
        + 1
        + 4
        + 4
        + 1
        + 4
        + 2
        + 4
        + 1
        + (2 + LANGUAGE.len())
}

#[test]
fn v1_self_consistency() {
    let bytes = encode(SCHEMA_V1);
    assert_eq!(&bytes[..5], MAGIC, "magic");
    assert_eq!(&bytes[5..9], &SCHEMA_V1.to_be_bytes(), "schema v1");
    let t = tz_start();
    assert_eq!(&bytes[t..t + 2], &[0, TIMEZONE.len() as u8], "tz length");
    assert_eq!(
        &bytes[t + 2..t + 2 + TIMEZONE.len()],
        TIMEZONE.as_bytes(),
        "tz"
    );
    let after = t + 2 + TIMEZONE.len();
    assert_eq!(
        &bytes[after..after + 2],
        &SLOT_COUNT.to_be_bytes(),
        "slot_count"
    );
}

#[test]
fn v2_self_consistency() {
    let bytes = encode(SCHEMA_V2);
    assert_eq!(&bytes[5..9], &SCHEMA_V2.to_be_bytes(), "schema v2");
    let t = tz_start();
    assert_eq!(
        &bytes[t + 2..t + 2 + TIMEZONE.len()],
        TIMEZONE.as_bytes(),
        "tz"
    );
    let after = t + 2 + TIMEZONE.len();
    assert_eq!(bytes[after], WEBRTC_POLICY, "policy after timezone");
    assert_eq!(
        &bytes[after + 1..after + 3],
        &SLOT_COUNT.to_be_bytes(),
        "slot_count"
    );
    // The slot block is byte-identical across schemas, shifted by the policy byte.
    let mut block = Vec::new();
    encode_slot_block(&mut block);
    assert_eq!(&bytes[after + 3..], &block[..], "v2 slot block");
    let v1 = encode(SCHEMA_V1);
    assert_eq!(&v1[after + 2..], &block[..], "v1 slot block");
    assert_eq!(bytes.len(), v1.len() + 1, "policy byte is the only growth");
}

fn encode_slot_block(out: &mut Vec<u8>) {
    for (name, slot) in SLOTS {
        text(out, name, true);
        num(out, slot as u64, 4);
    }
}

#[test]
fn encoding_is_deterministic() {
    assert_eq!(encode(SCHEMA_V1), encode(SCHEMA_V1));
    assert_eq!(encode(SCHEMA_V2), encode(SCHEMA_V2));
    assert_eq!(encode(SCHEMA_V1).len(), 147);
    assert_eq!(encode(SCHEMA_V2).len(), 148);
}

#[test]
fn v1_matches_native_golden_inc() {
    let encoded = encode(SCHEMA_V1);
    let fixture = golden_fixture(&PathBuf::from(env!("GOLDEN_V1_PATH")));
    if encoded != fixture {
        panic!(
            "v1 MISMATCH: rust len {} vs fixture len {}; first differing byte offset {:?}",
            encoded.len(),
            fixture.len(),
            first_diff(&encoded, &fixture)
        );
    }
}

#[test]
fn v2_matches_w7_snapshot() {
    let encoded = encode(SCHEMA_V2);
    let fixture = golden_fixture(&PathBuf::from(env!("GOLDEN_V2_SNAPSHOT")));
    if encoded != fixture {
        // Report precise offsets: per-byte diff plus field-context hints.
        let mut diffs = Vec::new();
        let n = encoded.len().max(fixture.len());
        for i in 0..n {
            let a = encoded.get(i).copied();
            let b = fixture.get(i).copied();
            if a != b {
                diffs.push((i, a, b));
            }
        }
        panic!(
            "v2 MISMATCH: rust len {} vs snapshot len {}; differing offsets: {:?}",
            encoded.len(),
            fixture.len(),
            diffs
        );
    }
}

#[test]
fn write_outputs_and_compare_python() {
    let dir = reviews_dir();
    let v1 = encode(SCHEMA_V1);
    let v2 = encode(SCHEMA_V2);
    std::fs::write(dir.join("golden_v1_rust.bin"), &v1).expect("write v1");
    std::fs::write(dir.join("golden_v2_rust.bin"), &v2).expect("write v2");
    // The Python reference must already exist (generated independently).
    let py1_path = dir.join("golden_v1_python.bin");
    let py2_path = dir.join("golden_v2_python.bin");
    for (name, rust, py) in [("v1", &v1, py1_path.clone()), ("v2", &v2, py2_path.clone())] {
        let pybytes = std::fs::read(&py)
            .unwrap_or_else(|e| panic!("python {name} output missing ({}): {e}", py.display()));
        if *rust != pybytes {
            panic!(
                "{name} rust-vs-python MISMATCH: offsets {:?}",
                rust.iter()
                    .zip(pybytes.iter())
                    .position(|(a, b)| a != b)
                    .map(|i| vec![i])
                    .unwrap_or_default()
            );
        }
    }
}

fn main() {
    // Not used under `rustc --test`; kept so the binary can also run bare.
    let dir = reviews_dir();
    std::fs::write(dir.join("golden_v1_rust.bin"), encode(SCHEMA_V1)).unwrap();
    std::fs::write(dir.join("golden_v2_rust.bin"), encode(SCHEMA_V2)).unwrap();
}
