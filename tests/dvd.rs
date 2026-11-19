//! The DVD player (`dvd/dvd`) on an emulated 192, started in
//! the loader's place by the boot ROM's stand-in: no firmware needed,
//! only an SH-4 GCC to build the program and a DVD-Video image, given
//! as `RNSE_DVD` (`dvd/check.sh` makes one with ffmpeg,
//! dvdauthor and genisoimage, and runs this):
//!
//!     RNSE_DVD=out/dvd/ntsc.iso cargo test --release --test dvd -- --nocapture
//!
//! Without either the test returns at once and passes.  `RNSE_DVD_SHOTS`
//! names a directory for screenshots of the run.
//!
//! `tests/disc.rs`, `dvd_player_from_the_sd_loader`, starts the same
//! program the way a unit does, through the stock software and the SD
//! loader; that one needs the firmware as well.

use rnse::board::Model;
use rnse::machine::Machine;
use rnse::sh4::cpu::Stop;

fn build() -> Option<Vec<u8>> {
    build_with("")
}

/// The program built with EXTRA_CFLAGS (read before another test can
/// build it otherwise).
fn build_with(extra: &str) -> Option<Vec<u8>> {
    let sdk = std::env::var_os("RNSE_SDK").map(std::path::PathBuf::from).unwrap_or_else(|| std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../sdk"));
    static BUILD: std::sync::Mutex<()> = std::sync::Mutex::new(());
    let _lock = BUILD.lock().unwrap_or_else(|e| e.into_inner());
    let out = std::process::Command::new("sh").arg(sdk.join("build.sh")).arg("192").arg(std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("dvd/dvd"))
        .env("EXTRA_CFLAGS", extra).output().ok()?;
    if !out.status.success() {
        let text = String::from_utf8_lossy(&out.stderr);
        // A compiler that is there and fails is a failure; none is a skip.
        assert!(text.contains("gcc: not found") || text.contains("gcc: command not found"), "build: {}", text);
        return None;
    }
    let program = std::fs::read(sdk.join("out/dvd-192.bin")).unwrap();
    if !extra.is_empty() {
        // sdk/out/dvd-192.bin is what goes on a card: the real one again.
        let again = std::process::Command::new("sh").arg(sdk.join("build.sh")).arg("192").arg(std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("dvd/dvd")).output().unwrap();
        assert!(again.status.success());
    }
    Some(program)
}

/// The program where the stock loader would be, started as the boot ROM
/// starts a loader.
fn start(program: &[u8], dvd: Vec<u8>) -> Machine {
    let mut m = Machine::for_model(Model::Rnse192);
    m.echo_console = std::env::var_os("RNSE_DVD_ECHO").is_some();
    m.scramble_ram();
    m.install_boot_stub("V222");
    m.board.load(0x0df0_0080, program);
    m.cpu.reset();
    m.cpu.set_sr(0x7000_00f0);
    m.cpu.pc = u32::from_be_bytes([program[0], program[1], program[2], program[3]]);
    m.cpu.r[15] = 0x8df1_fff0;
    m.busy_start();
    m.watchdog_always = true;
    m.cache_model();
    // RNSE_DVD_TIMES: the drive at the pace measured on a 192 with a CD
    // (5.24 ms a sector); otherwise it answers at once.
    m.atapi().times = std::env::var_os("RNSE_DVD_TIMES").is_some();
    m.atapi().load(dvd);
    m
}

fn shot(m: &Machine, name: &str) {
    if let Some(dir) = std::env::var_os("RNSE_DVD_SHOTS") {
        let (w, h, rgb) = m.render();
        let path = std::path::Path::new(&dir).join(name);
        std::fs::write(&path, rnse::png::encode_rgb(w, h, &rgb)).unwrap();
    }
}

fn press(m: &mut Machine, key: &str) {
    let ms = m.cycles_per_ms();
    assert!(m.panel_key(key, true));
    assert_eq!(m.run(200 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    assert!(m.panel_key(key, false));
    assert_eq!(m.run(200 * ms), Stop::Budget, "{:?}", m.cpu.fault);
}

fn console(m: &Machine) -> String {
    String::from_utf8_lossy(&m.console).into_owned()
}

/// Run until the console has `what`, for at most `seconds`.
fn until(m: &mut Machine, what: &str, seconds: u64) -> bool {
    let ms = m.cycles_per_ms();
    for _ in 0..seconds * 10 {
        if console(m).contains(what) {
            return true;
        }
        assert_eq!(m.run(100 * ms), Stop::Budget, "{:?}: {}", m.cpu.fault, console(m));
    }
    console(m).contains(what)
}

/// How much of the picture is neither black nor grey: the test pictures
/// are colour bars and gradients.
fn colourful(m: &Machine) -> usize {
    let (_, _, rgb) = m.render();
    rgb.chunks(3).filter(|p| {
        let (r, g, b) = (p[0] as i32, p[1] as i32, p[2] as i32);
        (r - g).abs() > 40 || (g - b).abs() > 40 || (r - b).abs() > 40
    }).count()
}

#[test]
fn dvd_player_plays_a_disc() {
    let Some(path) = std::env::var_os("RNSE_DVD") else { return };
    let Some(program) = build() else { return };
    let dvd = std::fs::read(path).expect("RNSE_DVD is not readable");
    let mut m = start(&program, dvd);
    let ms = m.cycles_per_ms();
    assert!(until(&mut m, "rnse sdk: dvd", 5), "{}", console(&m));
    // The drive spins the disc up (some seconds on a 192), the titles
    // are read, and the list is on the screen.
    assert!(until(&mut m, "READING THE DISC", 30), "{}", console(&m));
    assert_eq!(m.run(3000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    shot(&m, "titles.png");
    assert!(!console(&m).contains("NOT A DVD"), "{}", console(&m));
    m.speaker = Some(Vec::new());
    press(&mut m, "SELECT");
    press(&mut m, "INFO");
    // Louder: the amplifier takes 2 dB a step off full volume, and the
    // test tone is at an eighth of full scale.
    assert!(m.panel_knob("volume", 12));
    let start = m.cpu.cycles;
    // RNSE_DVD_PROFILE: where the CPU is while it plays, for
    // tools/profile.py sdk/out/dvd-192.elf FILE.
    if std::env::var_os("RNSE_DVD_PROFILE").is_some() {
        m.profile = Some(std::collections::HashMap::new());
    }
    assert_eq!(m.run(2500 * ms), Stop::Budget, "{:?}: {}", m.cpu.fault, console(&m));
    shot(&m, "playing.png");
    let colour = colourful(&m);
    assert!(colour > 20_000, "{} colourful pixels: {}", colour, console(&m));
    // CAR: the picture at 8 bits a pixel (the fallback, should a unit
    // not show the 16-bit layer).
    press(&mut m, "CAR");
    assert_eq!(m.run(300 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    shot(&m, "8bit.png");
    let colour = colourful(&m);
    assert!(colour > 20_000, "{} colourful pixels at 8 bits: {}", colour, console(&m));
    press(&mut m, "CAR");
    // The chapter after.
    press(&mut m, "NEXT");
    assert_eq!(m.run(1500 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    shot(&m, "chapter.png");
    let colour = colourful(&m);
    assert!(colour > 20_000, "{} colourful pixels after NEXT: {}", colour, console(&m));
    if let (Some(file), Some(profile)) = (std::env::var_os("RNSE_DVD_PROFILE"), m.profile.take()) {
        let text: String = profile.iter().map(|(pc, n)| format!("{:08x} {}\n", pc, n)).collect();
        std::fs::write(file, text).unwrap();
    }
    let secs = (m.cpu.cycles - start) as f64 / ms as f64 / 1000.0;
    // Sound came out: the amplifier had the card processor's output.
    let speaker = m.speaker.take().unwrap();
    let loud = speaker.iter().filter(|s| s.abs() > 500).count();
    if let Some(dir) = std::env::var_os("RNSE_DVD_SHOTS") {
        let bytes: Vec<u8> = speaker.iter().flat_map(|s| s.to_le_bytes()).collect();
        std::fs::write(std::path::Path::new(&dir).join("speaker.s16"), bytes).unwrap();
    }
    println!("{:.1} s played, {} samples out, {} loud; {}", secs, speaker.len(), loud, console(&m));
    assert!(loud > 40_000, "{} of {} samples loud: {}", loud, speaker.len(), console(&m));
    // RETURN: back to the list.
    press(&mut m, "RETURN");
    assert_eq!(m.run(1000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    shot(&m, "back.png");
    assert!(colourful(&m) < 5_000);
    let text = console(&m);
    assert!(!text.contains("unexpected"), "{}", text);
    assert!(!text.contains("ENCRYPTED"), "{}", text);
    let cache = &m.cpu.cache.trouble;
    assert_eq!((cache.stale_fetches, cache.dirty_discarded), (0, 0), "{:?}", cache);
}

/// The disc with the scrambling bits of every video packet set, as they
/// are on a commercial disc.  (Its data is the plain data still.)
fn marked(mut dvd: Vec<u8>) -> Vec<u8> {
    let mut marked = 0;
    for sector in dvd.chunks_mut(2048) {
        // A pack (MPEG-2 header) with a PES packet of the video stream.
        if sector.len() < 2048 || sector[..4] != [0, 0, 1, 0xba] || sector[4] & 0xc0 != 0x40 {
            continue;
        }
        let mut o = 14 + (sector[13] & 7) as usize;
        while o + 9 <= 2048 && sector[o..o + 3] == [0, 0, 1] {
            let len = u16::from_be_bytes([sector[o + 4], sector[o + 5]]) as usize;
            if sector[o + 3] == 0xe0 && sector[o + 6] & 0xc0 == 0x80 {
                sector[o + 6] |= 0x10;
                marked += 1;
            }
            o += 6 + len;
        }
    }
    assert!(marked > 100, "{} video packets marked", marked);
    dvd
}

/// The extension point for a descrambler (sdk/examples/dvd/scramble.c):
/// built with SCRAMBLE_TEST, a stand-in that only clears the marks of
/// the marked disc, whose data is plain.  The title plays: the hooks are
/// called at the title's start and for its scrambled sectors.
#[test]
fn dvd_player_hands_scrambled_sectors_to_a_descrambler() {
    let Some(path) = std::env::var_os("RNSE_DVD") else { return };
    let Some(program) = build_with("-DSCRAMBLE_TEST") else { return };
    let dvd = marked(std::fs::read(path).expect("RNSE_DVD is not readable"));
    let mut m = start(&program, dvd);
    assert!(until(&mut m, "READING THE DISC", 30), "{}", console(&m));
    let ms = m.cycles_per_ms();
    assert_eq!(m.run(3000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    press(&mut m, "SELECT");
    assert_eq!(m.run(3000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    shot(&m, "descrambled.png");
    let text = console(&m);
    assert!(text.contains("scramble_open"), "{}", text);
    assert!(!text.contains("ENCRYPTED"), "{}", text);
    let colour = colourful(&m);
    assert!(colour > 20_000, "{} colourful pixels: {}", colour, text);
}

/// A disc whose video is scrambled (CSS) is recognised and refused by
/// the player as it is, which has no descrambler: the marked test disc.
#[test]
fn dvd_player_refuses_a_scrambled_disc() {
    let Some(path) = std::env::var_os("RNSE_DVD") else { return };
    let Some(program) = build() else { return };
    let dvd = marked(std::fs::read(path).expect("RNSE_DVD is not readable"));
    let mut m = start(&program, dvd);
    assert!(until(&mut m, "READING THE DISC", 30), "{}", console(&m));
    let ms = m.cycles_per_ms();
    assert_eq!(m.run(3000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    press(&mut m, "SELECT");
    assert!(until(&mut m, "ENCRYPTED", 10), "{}", console(&m));
    shot(&m, "scrambled.png");
    // RETURN: the list again.
    press(&mut m, "RETURN");
    assert_eq!(m.run(1000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    assert!(!console(&m).contains("unexpected"), "{}", console(&m));
}
