//! The ports' end-to-end tests: Doom, Quake, umac, Windows 3.11, the NES
//! emulator and the DVD player started the way a unit starts them (the
//! SD loader, put on an update disc in the loader's place and started by
//! the stock software) and run on the emulator.
//!
//!     RNSE_DISC=/path/to/disc.iso cargo test --release -p rnse-ports -- --nocapture
//!
//! They need `RNSE_DISC`, an image of an update CD (firmware, never in a
//! repository), an SH-4 GCC on the PATH (the kit's `tools/sh4-docker`
//! has one in a container), and each its own built game and data:
//! `RNSE_DOOM`, `RNSE_QUAKE`, `RNSE_UMAC` with `RNSE_MAC_ROM` and
//! `RNSE_MAC_DISK`, `RNSE_WIN311`, `RNSE_DVD`, and for the NES `nes/build.sh`
//! run first (and `RNSE_NES_ROM` for a ROM of one's own).  Without them
//! they return early and pass, which proves nothing.  The kit is
//! `RNSE_SDK`, or rnse's `sdk/` beside this directory.  The helpers are
//! copies of rnse's `tests/disc.rs`, taking a program of this repository
//! by path.

use rnse::fw;
use rnse::machine::Machine;
use rnse::sh4::cpu::Stop;

/// The kit: RNSE_SDK, or rnse's sdk/ beside this directory.
fn kit() -> std::path::PathBuf {
    std::env::var_os("RNSE_SDK").map(std::path::PathBuf::from)
        .unwrap_or_else(|| std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../rnse-sdk"))
}

/// A program of this repository, as build.sh takes it (a path).
fn own(program: &str) -> String {
    std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join(program).to_string_lossy().into_owned()
}

/// What build.sh names a program's output after: its last component.
fn output_name(example: &str) -> &str {
    example.rsplit('/').next().unwrap()
}

fn disc() -> Option<Vec<u8>> {
    let path = std::env::var_os("RNSE_DISC")?;
    Some(std::fs::read(path).expect("RNSE_DISC is not readable"))
}

/// A program written from scratch (sdk/examples/hello), built with an
/// SH-4 GCC if there is one, put in the loader's place on the disc and
/// started the way the boot ROM starts the loader: it brings the board
/// up, prints on the debug serial port, draws and keeps time, on both
/// units.
/// Build an SDK example for a unit, put it in the loader's place and
/// start it.  None when there is no cross compiler or the disc has no
/// software for that unit.
fn sdk_example(image: &[u8], model: rnse::board::Model, example: &str) -> Option<(Machine, String, Vec<u8>)> {
    use rnse::board::Model;
    let sdk = kit();
    let unit = if model == Model::Rnse192 { "192" } else { "193" };
    let variants = fw::load_variants(image).unwrap();
    let v = variants.iter().find(|v| Model::for_variant(&v.name) == Some(model))?;
    // Builds of the same example for the two tests that use it must
    // not run into each other.
    static BUILD: std::sync::Mutex<()> = std::sync::Mutex::new(());
    let built = {
        let _lock = BUILD.lock().unwrap();
        std::process::Command::new("sh").arg(sdk.join("build.sh")).args([unit, example]).output()
    };
    match built {
        Ok(out) if out.status.success() => {}
        Ok(out) => {
            // No cross compiler here: nothing to test.  A compiler that
            // is there and fails (a program that no longer fits, say)
            // is a failure, not a skip.
            let text = String::from_utf8_lossy(&out.stderr);
            assert!(text.contains("gcc: not found") || text.contains("gcc: command not found"),
                "{} for the {}: the build failed: {}", example, unit, text);
            return None;
        }
        Err(_) => return None,
    }
    let program = std::fs::read(sdk.join(format!("out/{}-{}.bin", output_name(example), unit))).unwrap();
    let (custom, places) = fw::custom_loader(image, &v.name, &program).unwrap();
    assert_eq!(places.len(), 2, "{}: the loader is on the disc in the container and as a file: {:?}", v.name, places);
    let variants = fw::load_variants(&custom).unwrap();
    let v = variants.iter().find(|x| x.name == v.name).unwrap();
    let ldm = v.module("LDM").unwrap();
    assert!(ldm.checksum_ok());

    let mut m = Machine::for_model(model);
    m.echo_console = false;
    // Whatever was in memory before: not zeroes.
    m.scramble_ram();
    m.install_boot_stub(&v.name);
    m.atapi().load(custom.clone());
    m.start_loader(ldm, [0; 3]);
    // As unforgiving as the emulator gets (`--strict`).
    m.busy_start();
    m.watchdog_always = true;
    m.cache_model();
    Some((m, v.name.clone(), custom))
}

/// An SDK example on a disc with a higher software index than the
/// installed one, and the installed (stock) software run until its
/// application has checked the disc, copied the program to RAM and is
/// about to call its first entry point.  That is how a real unit would
/// come to run it.  None when there is no cross compiler or the disc
/// has no software for the unit.
fn started_by_stock_software(image: &[u8], model: rnse::board::Model, example: &str) -> Option<(Machine, String, Vec<u8>)> {
    started_by_stock_software_with(image, model, example, None)
}

/// The same, with the disc in the drive from the start (None) or put
/// in after so many milliseconds, as someone would do it.
fn started_by_stock_software_with(image: &[u8], model: rnse::board::Model, example: &str, insert_at: Option<u64>) -> Option<(Machine, String, Vec<u8>)> {
    use rnse::board::Model;
    let sdk = kit();
    let variants = fw::load_variants(image).unwrap();
    let v = variants.iter().find(|v| Model::for_variant(&v.name) == Some(model))?;
    sdk_example(image, model, example)?;
    let unit = if model == Model::Rnse192 { "192" } else { "193" };
    let program = std::fs::read(sdk.join(format!("out/{}-{}.bin", output_name(example), unit))).unwrap();
    let (mut custom, _) = fw::custom_loader(image, &v.name, &program).unwrap();
    // The disc's index, its two middle digits one up: those are what
    // the application compares.
    let index = fw::IndexInfo::from_disc(image, &v.name).unwrap();
    let middle: u32 = index.sofidx[1..3].parse().unwrap();
    let higher = format!("{}{:02}{}", &index.sofidx[..1], middle + 1, &index.sofidx[3..]);
    assert_eq!(fw::set_disc_index(&mut custom, &v.name, &higher).unwrap(), index.sofidx);

    // The stock software's own start of a loader can fail: on the
    // European 192 the copy of the loader goes over a variable of the
    // module APFM, whose timer handler (every 100 ms) then meets an
    // address error if it runs in the 15 ms before the loader has
    // taken the unit over (docs/boot-rom.md).  Whether it does is a
    // matter of timing, which on a real unit the drive makes vary and
    // which here is the same in every run.  So the start is tried at
    // the nominal ratio of CPU speed to timers and, when exactly that
    // happens, at the two next to it.
    let apfm = v.module("APFM").map(|a| a.load_addr | 0x8000_0000..(a.load_addr | 0x8000_0000) + a.size);
    let nominal = if model == Model::Rnse192 { 4 } else { 8 };
    let mut started = None;
    for ratio in [nominal, nominal + 1, nominal - 1] {
        let mut m = Machine::for_model(model);
        m.set_cpu_ratio(ratio);
        m.echo_console = false;
        // The CPU's caches are modelled all the way: the stock software has
        // to live with them as on a real unit, and so has the program.
        m.scramble_ram();
        m.cache_model();
        m.install_boot_stub(&v.name);
        m.install(v, &index);
        if insert_at.is_none() {
            m.atapi().load(custom.clone());
        }
        m.start_os(v).unwrap();
        m.set_ignition(true);
        let ms = m.cycles_per_ms();
        let entry = fw::load_variants(&custom).unwrap().iter().find(|x| x.name == v.name).unwrap().module("LDM").unwrap().entries[0];
        m.cpu.breakpoints.push(entry);
        let mut stop = Stop::Budget;
        if let Some(at) = insert_at {
            stop = m.run(at * ms);
            assert_eq!(stop, Stop::Budget, "{}: {:?}", v.name, m.cpu.fault);
            m.atapi().insert(Some(custom.clone()));
        }
        for _ in 0..40 {
            if stop != Stop::Budget {
                break;
            }
            stop = m.run(1_000 * ms);
        }
        let collision = stop == Stop::Fault && m.cpu.fault.as_deref().is_some_and(|f| {
            let spc = f.split("SPC=").nth(1).and_then(|t| u32::from_str_radix(&t[..8.min(t.len())], 16).ok());
            f.contains("EXPEVT=0e0") && spc.zip(apfm.clone()).is_some_and(|(spc, range)| range.contains(&spc))
        });
        if collision {
            eprintln!("{}: at CPU ratio {} the stock software's start of the loader collides with its own timer handler", v.name, ratio);
            continue;
        }
        assert_eq!(stop, Stop::Breakpoint, "{}: the installed software did not start the program: {:?}", v.name, m.cpu.fault);
        started = Some(m);
        break;
    }
    let mut m = started.unwrap_or_else(|| panic!("{}: the stock software's start of the loader failed at every speed tried", v.name));
    m.cpu.breakpoints.clear();
    Some((m, v.name.clone(), custom))
}

/// Run until the log has `what` after `from`, in steps of five seconds.
fn until(m: &mut Machine, from: usize, what: &str, seconds: u64) -> Option<usize> {
    let ms = m.cycles_per_ms();
    for _ in 0..seconds / 5 {
        assert_eq!(m.run(5_000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
        let text = String::from_utf8_lossy(&m.console);
        if let Some(at) = text.get(from..).and_then(|t| t.find(what)) {
            return Some(from + at);
        }
    }
    None
}

/// Doom (docs/doom.md) from an SD card, as on a unit: the stock software
/// starts `sdboot`, which starts `DOOM.BIN` (sdk/examples/doomboot) from
/// the card, which reads the game, DOOMGAME.BIN, and the shareware WAD
/// and enters the game.  The controls while the card is read; the title
/// screen and the demo; sound effects out of a card processor's
/// converter; the menu with the faceplate's keys and a game begun; the
/// display and the disc while it plays; RETURN held back to `sdboot`.
/// A PC on the 192 (win311/, Tiny386): sdboot starts pcboot
/// (WIN311.BIN) from the card, which starts the emulator (PCEMU.BIN),
/// which boots MS-DOS and Windows 3.11 from the disk image on the same
/// card, and the knobs move the mouse.  With the caches modelled and RAM
/// arbitrary.  Needs RNSE_WIN311, a directory with what win311/build.sh
/// writes (win311/out) and a disk image WIN311.IMG with DOS and Windows
/// installed (docs/win311.md: not in the repository).  Screenshots go to
/// out/win311/.
#[test]
fn win311_runs_from_an_sd_card() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    let Some(dir) = std::env::var_os("RNSE_WIN311") else { return };
    let dir = std::path::PathBuf::from(dir);
    let file = |name: &str| std::fs::read(dir.join(name)).unwrap_or_else(|_| panic!("RNSE_WIN311 has no {}", name));
    let Some((mut m, name, _)) = started_by_stock_software(&image, Model::Rnse192, "sdboot") else { return };
    let entries = vec![
        rnse::fat::Entry::file("WIN311.BIN", file("WIN311.BIN")),
        rnse::fat::Entry::file("PCEMU.BIN", file("PCEMU.BIN")),
        rnse::fat::Entry::file("BIOS.ROM", file("BIOS.ROM")),
        rnse::fat::Entry::file("VGABIOS.ROM", file("VGABIOS.ROM")),
        rnse::fat::Entry::file("WIN311.IMG", file("WIN311.IMG")),
    ];
    let card = rnse::fat::card("WIN311", &entries, None).unwrap();
    assert!(m.insert_card(0, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
    let ms = m.cycles_per_ms();
    m.watchdog.longest = 0;
    let console = |m: &Machine| String::from_utf8_lossy(&m.console).into_owned();
    let tail = |m: &Machine| { let t = console(m); t[t.len().saturating_sub(1500)..].to_string() };
    std::fs::create_dir_all("out/win311").unwrap();
    let shot = |m: &Machine, file: &str| {
        let (w, h, rgb) = m.render();
        std::fs::write(format!("out/win311/{file}.png"), rnse::png::encode_rgb(w, h, &rgb)).unwrap();
    };
    // Windows' desktop: the share of the screen in its grey (192, 192,
    // 192), and whether a title bar's dark blue (0, 0, 128) is there.
    let desktop = |m: &Machine| {
        let (_, _, rgb) = m.render();
        let px = rgb.len() / 3;
        let grey = rgb.chunks(3).filter(|p| p == &[192, 192, 192]).count();
        let blue = rgb.chunks(3).filter(|p| p == &[0, 0, 128]).count();
        (grey * 100 / px, blue)
    };

    until(&mut m, 0, "rnse sdk: pcemu", 60).unwrap_or_else(|| panic!("{}: the emulator did not start: {:?}", name, tail(&m)));
    until(&mut m, 0, "pcemu: WIN311.IMG", 30).unwrap_or_else(|| panic!("{}: no disk image: {:?}", name, tail(&m)));
    // Where the SH-4 spends its time, sampled (addresses to
    // out/win311/profile.txt, for nm).
    // RNSE_WIN311_PROFILE=S: after S seconds (Windows starting at about
    // 60), a second of samples.
    if let Some(skip) = std::env::var("RNSE_WIN311_PROFILE").ok().and_then(|s| s.parse::<u64>().ok()) {
        assert_eq!(m.run(skip * 1_000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
        shot(&m, "profiled");
        let mut hist = std::collections::HashMap::<u32, u32>::new();
        for _ in 0..120_000 {
            assert_eq!(m.run(2_000 + (hist.len() as u64 % 7)), Stop::Budget, "{:?}", m.cpu.fault);
            *hist.entry(m.cpu.pc).or_default() += 1;
        }
        let mut v: Vec<_> = hist.into_iter().collect();
        v.sort_by(|a, b| b.1.cmp(&a.1));
        let text: String = v.iter().map(|(pc, n)| format!("{:08x} {}\n", pc, n)).collect();
        std::fs::write("out/win311/profile.txt", text).unwrap();
        return;
    }
    let started = std::time::Instant::now();
    let mut seconds = 0;
    let mut up = None;
    while seconds < 240 {
        assert_eq!(m.run(5_000 * ms), Stop::Budget, "{}: {:?} {}", name, m.cpu.fault, tail(&m));
        seconds += 5;
        shot(&m, &format!("boot-{:03}", seconds));
        std::fs::write("out/win311/console.txt", &m.console).unwrap();
        let (grey, blue) = desktop(&m);
        eprintln!("win311: {} s emulated ({:.0} s here): grey {}%, blue {}", seconds, started.elapsed().as_secs_f64(), grey, blue);
        if grey > 15 && blue > 100 {
            up = Some(seconds);
            break;
        }
    }
    let up = up.unwrap_or_else(|| panic!("{}: no Windows desktop after {} s: {:?}", name, seconds, tail(&m)));
    eprintln!("win311: Program Manager after {} s of the unit's time", up);
    let longest = m.watchdog.longest / ms;
    assert!(longest < 40, "{}: the watchdog waited {} ms, until {} ms, PC {:08x}", name, longest, m.watchdog.longest_end / ms, m.watchdog.longest_pc);

    // Until the picture stands still (Program Manager's icons drawn).
    let mut last = m.render().2;
    for _ in 0..30 {
        assert_eq!(m.run(1_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        let now = m.render().2;
        if now == last { break }
        last = now;
    }
    shot(&m, "desktop");
    eprintln!("win311: {}", tail(&m).lines().filter(|l| l.starts_with("pcemu:")).last().unwrap_or(""));

    // The pointer: what changes between two pictures, as a box.
    let w = m.render().0;
    let changed = |a: &[u8], b: &[u8]| {
        let mut bx = (usize::MAX, usize::MAX, 0, 0);
        for (i, (p, q)) in a.chunks(3).zip(b.chunks(3)).enumerate() {
            if p != q {
                let (x, y) = (i % w, i / w);
                bx = (bx.0.min(x), bx.1.min(y), bx.2.max(x), bx.3.max(y));
            }
        }
        bx
    };
    let turn = |m: &mut Machine, knob: &str, n: i32| -> Vec<u8> {
        for _ in 0..n.abs() {
            assert!(m.panel_knob(knob, n.signum()));
            assert_eq!(m.run(60 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        }
        assert_eq!(m.run(1_500 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        m.render().2
    };
    // Slowly, a detent at a time: the volume knob louder moves it right,
    // the select knob clockwise down, a few pixels a detent.
    let right = turn(&mut m, "volume", 10);
    shot(&m, "pointer-right");
    let (x0, y0, x1, y1) = changed(&last, &right);
    eprintln!("win311: right: changed ({}, {})..({}, {})", x0, y0, x1, y1);
    assert!(x0 != usize::MAX, "{}: the volume knob did not move the pointer", name);
    assert!(x1 - x0 >= 8 && x1 - x0 <= 40 && y1 - y0 <= 24, "{}: moved ({}, {})..({}, {}) for 10 detents across", name, x0, y0, x1, y1);
    let down = turn(&mut m, "main", 10);
    shot(&m, "pointer-down");
    let (x0, y0, x1, y1) = changed(&right, &down);
    eprintln!("win311: down: changed ({}, {})..({}, {})", x0, y0, x1, y1);
    assert!(x0 != usize::MAX, "{}: the select knob did not move the pointer", name);
    assert!(y1 - y0 >= 6 && y1 - y0 <= 40 && x1 - x0 <= 24, "{}: moved ({}, {})..({}, {}) for 10 detents down", name, x0, y0, x1, y1);
}

/// With the caches modelled and RAM arbitrary.  Needs RNSE_DOOM, the
/// directory doom/build.sh writes (doom/out: DOOMGAME.BIN, DOOM1.WAD).
#[test]
fn doom_runs_from_an_sd_card() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    let Some(dir) = std::env::var_os("RNSE_DOOM") else { return };
    let dir = std::path::PathBuf::from(dir);
    let game = std::fs::read(dir.join("DOOMGAME.BIN")).expect("RNSE_DOOM has no DOOMGAME.BIN");
    let wad = std::fs::read(dir.join("DOOM1.WAD")).expect("RNSE_DOOM has no DOOM1.WAD");
    let sdk = kit();
    let Some((mut m, name, _)) = started_by_stock_software(&image, Model::Rnse192, "sdboot") else { return };
    if sdk_example(&image, Model::Rnse192, &own("doom/doomboot")).is_none() { return }
    let doomboot = std::fs::read(sdk.join("out/doomboot-192.bin")).unwrap();
    let entries = vec![
        rnse::fat::Entry::file("DOOM.BIN", doomboot),
        rnse::fat::Entry::file("DOOMGAME.BIN", game),
        rnse::fat::Entry::file("DOOM1.WAD", wad),
    ];
    let card = rnse::fat::card("DOOM", &entries, None).unwrap();
    assert!(m.insert_card(0, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
    let ms = m.cycles_per_ms();
    m.watchdog.longest = 0;
    let console = |m: &Machine| String::from_utf8_lossy(&m.console).into_owned();
    let tail = |m: &Machine| { let t = console(m); t[t.len().saturating_sub(800)..].to_string() };
    let press = |m: &mut Machine, key: &str, hold: u64| {
        assert!(m.panel_key(key, true));
        assert_eq!(m.run(hold * ms), Stop::Budget, "{:?}", m.cpu.fault);
        assert!(m.panel_key(key, false));
    };
    // Doom's 320x200, centred on the 400x235 screen: how many colours,
    // and the pixels themselves.
    let window = |m: &Machine| {
        let (w, h, pixels) = m.render();
        let bytes = pixels.len() / (w * h);
        let mut out = Vec::new();
        for y in 17..217 {
            out.extend_from_slice(&pixels[(y * w + 40) * bytes..(y * w + 360) * bytes]);
        }
        let colours: std::collections::HashSet<&[u8]> = out.chunks(bytes).collect();
        (colours.len(), out.clone())
    };

    // While the card is read: the display folds down and up.
    until(&mut m, 0, "READING DOOM1.WAD", 60).unwrap_or_else(|| panic!("{}: doomboot did not read the card: {:?}", name, tail(&m)));
    press(&mut m, "EJECT", 200);
    assert_eq!(m.run(2_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    assert!(m.board.display_pos <= 94, "{}: the display is at {} while the card is read", name, m.board.display_pos);
    press(&mut m, "EJECT", 200);
    assert_eq!(m.run(2_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    assert!(m.board.display_pos >= 894, "{}: the display is at {}", name, m.board.display_pos);

    // The game: its start, then the title and the demo, which plays
    // (the picture changes) and is heard.
    until(&mut m, 0, "ST_Init: Init status bar.", 60).unwrap_or_else(|| panic!("{}: Doom did not start: {:?}", name, tail(&m)));
    let text = console(&m);
    assert!(text.lines().any(|l| l.trim_end() == "rnse sdk: doom") && text.contains("adding DOOM1.WAD") && text.contains("DOOM Shareware"), "{}: {:?}", name, tail(&m));
    assert!(text.contains("sound: card processor 0"), "{}: no sound: {:?}", name, tail(&m));
    assert_eq!(m.run(12_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    let (colours, first) = window(&m);
    assert!(colours > 40, "{}: {} colours in Doom's window", name, colours);
    assert_eq!(m.run(1_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    let (_, second) = window(&m);
    assert_ne!(first, second, "{}: the demo does not move", name);
    let dsp = &m.board.media.dsps[0];
    assert!(dsp.fault.is_none(), "{}: {:?}", name, dsp.fault);
    assert!(dsp.loud_frames > 0, "{}: the card processor sends its converter only silence ({} samples)", name, dsp.frames);

    // The menu (SETUP), New Game, the episode, the skill: E1M1.  Then
    // forward, a turn, the door key.
    let before = console(&m).len();
    press(&mut m, "SETUP", 150);
    assert_eq!(m.run(800 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    for _ in 0..3 {
        press(&mut m, "SELECT", 150);
        assert_eq!(m.run(800 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    }
    until(&mut m, before, "doom: playing E1M1", 10).unwrap_or_else(|| panic!("{}: no game began: {:?}", name, tail(&m)));
    let (_, standing) = window(&m);
    press(&mut m, "NEXT", 1_000);
    assert!(m.panel_knob("main", 4));
    assert_eq!(m.run(500 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    let (_, moved) = window(&m);
    assert_ne!(standing, moved, "{}: the view did not change with NEXT and the knob", name);

    // While it plays: the display folds down, the disc comes out, the
    // display folds up; a short RETURN is Doom's (the menu), not the way
    // back.
    press(&mut m, "EJECT", 200);
    assert_eq!(m.run(2_500 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    assert!(m.board.display_pos <= 94, "{}: the display is at {} in the game", name, m.board.display_pos);
    let ejects = m.atapi().ejects;
    press(&mut m, "CD_EJECT", 200);
    assert_eq!(m.run(2_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    assert_eq!(m.atapi().ejects, ejects + 1, "{}: no eject in the game", name);
    press(&mut m, "EJECT", 200);
    assert_eq!(m.run(2_500 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    assert!(m.board.display_pos >= 894, "{}: the display is at {}", name, m.board.display_pos);
    press(&mut m, "RETURN", 300);
    assert_eq!(m.run(1_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    assert!(!console(&m)[before..].contains("rnse sdk: sdboot"), "{}: a short RETURN left the game", name);

    // RETURN held: back to sdboot, with the status of a RETURN held.
    let before = console(&m).len();
    press(&mut m, "RETURN", 2_500);
    assert_eq!(m.run(3_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    let text = console(&m);
    assert!(text[before..].contains("rnse sdk: sdboot") && text[before..].contains("TEST ENDED BY HOLDING RETURN"), "{}: {:?}", name, &text[before..]);
    assert!(!text[before..].lines().any(|l| l.trim_end() == "rnse sdk: doom"), "{}: Doom was started again", name);

    // The watchdog was served throughout (it resets the unit 105 ms after
    // the last edge), and the caches never handed out anything stale.
    let longest = m.watchdog.longest / ms;
    assert!(longest < 40, "{}: the watchdog waited {} ms, until {} ms, PC {:08x}", name, longest, m.watchdog.longest_end / ms, m.watchdog.longest_pc);
    let cache = &m.cpu.cache.trouble;
    assert_eq!((cache.stale_fetches, cache.dirty_discarded), (0, 0), "{}: {:?}", name, cache);
}

/// umac, a Mac Plus emulator, on the 192 from an SD card (docs/umac.md):
/// the stock software starts sdboot, sdboot's only program is macboot
/// (MAC.BIN), which reads umac (MACEMU.BIN), the Mac Plus ROM and a disk
/// image and starts umac.  Needs RNSE_UMAC=umac/out (umac/build.sh) and,
/// since neither may be in the repository, RNSE_MAC_ROM (the Mac Plus's
/// third ROM, 128 KiB) and RNSE_MAC_DISK (a raw image of a disk with a
/// System that boots).  Screenshots go to out/umac/.
#[test]
fn mac_runs_from_an_sd_card() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    let (Some(dir), Some(rom), Some(disk)) =
        (std::env::var_os("RNSE_UMAC"), std::env::var_os("RNSE_MAC_ROM"), std::env::var_os("RNSE_MAC_DISK"))
    else {
        return;
    };
    let umac = std::fs::read(std::path::Path::new(&dir).join("MACEMU.BIN")).expect("RNSE_UMAC has no MACEMU.BIN");
    let rom = std::fs::read(rom).expect("RNSE_MAC_ROM is not readable");
    let disk = std::fs::read(disk).expect("RNSE_MAC_DISK is not readable");
    let sdk = kit();
    let Some((mut m, name, _)) = started_by_stock_software(&image, Model::Rnse192, "sdboot") else { return };
    if sdk_example(&image, Model::Rnse192, &own("umac/macboot")).is_none() { return }
    let macboot = std::fs::read(sdk.join("out/macboot-192.bin")).unwrap();
    let entries = vec![
        rnse::fat::Entry::file("MAC.BIN", macboot),
        rnse::fat::Entry::file("MACEMU.BIN", umac),
        rnse::fat::Entry::file("MACPLUS.ROM", rom),
        rnse::fat::Entry::file("MAC.DSK", disk),
    ];
    let card = rnse::fat::card("MAC", &entries, None).unwrap();
    assert!(m.insert_card(0, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
    let ms = m.cycles_per_ms();
    m.watchdog.longest = 0;
    let console = |m: &Machine| String::from_utf8_lossy(&m.console).into_owned();
    let tail = |m: &Machine| { let t = console(m); t[t.len().saturating_sub(800)..].to_string() };
    let press = |m: &mut Machine, key: &str, hold: u64| {
        assert!(m.panel_key(key, true));
        assert_eq!(m.run(hold * ms), Stop::Budget, "{:?}", m.cpu.fault);
        assert!(m.panel_key(key, false));
    };
    // The Mac's 384x235 at x = 8 of the 400x235 screen, black (true) or
    // white, one bool a pixel, saved as a PNG on the way.
    let shot = |m: &Machine, file: &str| -> Vec<Vec<bool>> {
        let (w, h, rgb) = m.render();
        std::fs::create_dir_all("out/umac").unwrap();
        std::fs::write(format!("out/umac/{file}.png"), rnse::png::encode_rgb(w, h, &rgb)).unwrap();
        let bytes = rgb.len() / (w * h);
        (0..h).map(|y| (8..392).map(|x| rgb[(y * w + x) * bytes] < 128).collect()).collect()
    };

    until(&mut m, 0, "READING MAC.DSK", 60).unwrap_or_else(|| panic!("{}: macboot did not read the card: {:?}", name, tail(&m)));
    until(&mut m, 0, "umac: 384x235", 60).unwrap_or_else(|| panic!("{}: umac did not start: {:?}", name, tail(&m)));
    let text = console(&m);
    assert!(text.lines().any(|l| l.trim_end() == "rnse sdk: umac"), "{}: {:?}", name, tail(&m));

    // The Mac boots from the disk into the Finder: its menu bar is a
    // white band across the top over a black line (the pointer may
    // cross it), with the desktop's pattern below.
    let mut finder = None;
    for second in (5..=90).step_by(5) {
        assert_eq!(m.run(5_000 * ms), Stop::Budget, "{}: {:?} {:?}", name, m.cpu.fault, tail(&m));
        let pixels = shot(&m, &format!("boot-{second:02}s"));
        let line = (15..24).find(|&y| pixels[y].iter().filter(|&&b| b).count() >= 370);
        if let Some(y) = line {
            if pixels[y + 4].iter().any(|&b| b) && pixels[y + 4].iter().any(|&b| !b) {
                finder = Some((second, y));
                break;
            }
        }
    }
    let (second, menu_line) = finder.unwrap_or_else(|| panic!("{}: no Finder after 90 s: {:?}", name, tail(&m)));
    eprintln!("{}: the Finder after about {} s, the menu bar's line at y = {}", name, second, menu_line);
    assert!(!console(&m).contains("Attempted to read"), "{}: {:?}", name, tail(&m));

    // The knobs move the pointer: the picture changes where it was and
    // where it goes, and nothing else does.
    assert_eq!(m.run(2_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    let before = shot(&m, "pointer-before");
    assert!(m.panel_knob("main", 6));
    assert!(m.panel_knob("volume", 6));
    assert_eq!(m.run(1_500 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    let after = shot(&m, "pointer-after");
    let changed: Vec<(usize, usize)> = (0..235).flat_map(|y| (0..384).map(move |x| (x, y)))
        .filter(|&(x, y)| before[y][x] != after[y][x]).collect();
    assert!(!changed.is_empty(), "{}: the knobs did not move the pointer", name);
    let (x0, x1) = (changed.iter().map(|c| c.0).min().unwrap(), changed.iter().map(|c| c.0).max().unwrap());
    let (y0, y1) = (changed.iter().map(|c| c.1).min().unwrap(), changed.iter().map(|c| c.1).max().unwrap());
    assert!(x1 - x0 < 120 && y1 - y0 < 120, "{}: more than the pointer changed: {}..{} x {}..{}", name, x0, x1, y0, y1);

    // The display folds down and up while the Mac runs.
    press(&mut m, "EJECT", 200);
    assert_eq!(m.run(2_500 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    assert!(m.board.display_pos <= 94, "{}: the display is at {} under the Mac", name, m.board.display_pos);
    press(&mut m, "EJECT", 200);
    assert_eq!(m.run(2_500 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    assert!(m.board.display_pos >= 894, "{}: the display is at {}", name, m.board.display_pos);

    // RETURN held: back to sdboot, with the status of a RETURN held.
    let before = console(&m).len();
    press(&mut m, "RETURN", 2_500);
    assert_eq!(m.run(3_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    let text = console(&m);
    assert!(text[before..].contains("rnse sdk: sdboot") && text[before..].contains("TEST ENDED BY HOLDING RETURN"), "{}: {:?}", name, &text[before..]);

    // The watchdog was served throughout, and the caches never handed
    // out anything stale.
    let longest = m.watchdog.longest / ms;
    assert!(longest < 40, "{}: the watchdog waited {} ms, until {} ms, PC {:08x}", name, longest, m.watchdog.longest_end / ms, m.watchdog.longest_pc);
    let cache = &m.cpu.cache.trouble;
    assert_eq!((cache.stale_fetches, cache.dirty_discarded), (0, 0), "{}: {:?}", name, cache);
}

/// Quake (quake/build.sh, docs/quake.md) from an SD card: sdboot starts
/// quakeboot (QUAKE.BIN), which reads the game (QUAKEGAM.BIN) and enters
/// it; the game reads PAK0.PAK from the card as it needs it and plays
/// its sound out of the other card processor.  Needs RNSE_QUAKE=quake/out
/// besides RNSE_DISC and a cross compiler.  RNSE_QUAKE_SHOTS=DIR keeps
/// screenshots of the way; RNSE_QUAKE_ARG=TEXT puts a QUAKE.ARG on the
/// card (with `+timedemo demo1` the test prints the frame rate and ends
/// there).
#[test]
fn quake_runs_from_an_sd_card() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    let Some(dir) = std::env::var_os("RNSE_QUAKE") else { return };
    let dir = std::path::PathBuf::from(dir);
    let game = std::fs::read(dir.join("QUAKEGAM.BIN")).expect("RNSE_QUAKE has no QUAKEGAM.BIN");
    let pak = std::fs::read(dir.join("PAK0.PAK")).expect("RNSE_QUAKE has no PAK0.PAK");
    let shots = std::env::var_os("RNSE_QUAKE_SHOTS").map(std::path::PathBuf::from);
    let sdk = kit();
    let Some((mut m, name, _)) = started_by_stock_software(&image, Model::Rnse192, "sdboot") else { return };
    if sdk_example(&image, Model::Rnse192, &own("quake/quakeboot")).is_none() { return }
    let quakeboot = std::fs::read(sdk.join("out/quakeboot-192.bin")).unwrap();
    let mut entries = vec![
        rnse::fat::Entry::file("QUAKE.BIN", quakeboot),
        rnse::fat::Entry::file("QUAKEGAM.BIN", game),
        rnse::fat::Entry::file("PAK0.PAK", pak),
    ];
    // RNSE_QUAKE_ARG: QUAKE.ARG for a run by hand (`+timedemo demo1`).
    let arg = std::env::var("RNSE_QUAKE_ARG").ok();
    if let Some(arg) = &arg {
        entries.push(rnse::fat::Entry::file("QUAKE.ARG", arg.as_bytes().to_vec()));
    }
    let card = rnse::fat::card("QUAKE", &entries, None).unwrap();
    assert!(m.insert_card(0, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
    let ms = m.cycles_per_ms();
    m.watchdog.longest = 0;
    let console = |m: &Machine| String::from_utf8_lossy(&m.console).into_owned();
    let tail = |m: &Machine| { let t = console(m); t[t.len().saturating_sub(1500)..].to_string() };
    let shot = |m: &Machine, what: &str| {
        if let Some(dir) = &shots {
            let (w, h, rgb) = m.render();
            std::fs::write(dir.join(format!("{}.png", what)), rnse::png::encode_rgb(w, h, &rgb)).unwrap();
        }
    };

    // Until the console says `what`, a second at a time, the console's
    // end shown when the unit stops.
    let wait_for = |m: &mut Machine, from: usize, what: &str, seconds: u64| -> Option<usize> {
        for _ in 0..seconds {
            let stop = m.run(1_000 * ms);
            let text = String::from_utf8_lossy(&m.console).into_owned();
            assert_eq!(stop, Stop::Budget, "{:?} at {} ms: {}", m.cpu.fault, m.cpu.cycles / ms, &text[text.len().saturating_sub(1500)..]);
            if let Some(at) = text.get(from..).and_then(|t| t.find(what)) {
                return Some(from + at);
            }
        }
        None
    };

    wait_for(&mut m, 0, "quake: PAK0.PAK", 60).unwrap_or_else(|| panic!("{}: the game did not start: {:?}", name, tail(&m)));
    wait_for(&mut m, 0, "Quake Initialized", 120).unwrap_or_else(|| { shot(&m, "fail"); panic!("{}: Quake did not come up: {:?}", name, tail(&m)) });
    if arg.as_deref().is_some_and(|a| a.contains("timedemo")) {
        let at = wait_for(&mut m, 0, " fps", 600).unwrap_or_else(|| panic!("{}: no timedemo result: {:?}", name, tail(&m)));
        eprintln!("timedemo: {}", console(&m)[at.saturating_sub(60)..at + 4].trim());
        return;
    }
    let press = |m: &mut Machine, key: &str, hold: u64| {
        assert!(m.panel_key(key, true));
        assert_eq!(m.run(hold * ms), Stop::Budget, "{:?}", m.cpu.fault);
        assert!(m.panel_key(key, false));
    };
    // Quake's 320x200, centred on the 400x235 screen: how many colours,
    // and the pixels themselves.
    let window = |m: &Machine| {
        let (w, h, pixels) = m.render();
        let bytes = pixels.len() / (w * h);
        let mut out = Vec::new();
        for y in 17..217 {
            out.extend_from_slice(&pixels[(y * w + 40) * bytes..(y * w + 360) * bytes]);
        }
        let colours: std::collections::HashSet<&[u8]> = out.chunks(bytes).collect();
        (colours.len(), out.clone())
    };
    let text = console(&m);
    let silent = arg.as_deref().is_some_and(|a| a.contains("-nosound"));
    assert!(silent || text.contains("quake: sound from card processor 1"), "{}: no sound: {:?}", name, tail(&m));

    // The first demo, read from the card as it plays: the picture moves
    // and is heard.
    wait_for(&mut m, 0, "Playing demo from demo1.dem", 30).unwrap_or_else(|| panic!("{}: no demo: {:?}", name, tail(&m)));
    assert_eq!(m.run(10_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    shot(&m, "demo");
    let (colours, first) = window(&m);
    assert!(colours > 40, "{}: {} colours in Quake's window", name, colours);
    assert_eq!(m.run(1_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    let (_, second) = window(&m);
    assert_ne!(first, second, "{}: the demo does not move", name);
    let dsp = &m.board.media.dsps[1];
    assert!(dsp.fault.is_none(), "{}: {:?}", name, dsp.fault);
    assert!(silent || dsp.loud_frames > 0, "{}: the other card processor sends its converter only silence ({} samples)", name, dsp.frames);

    // While the card is read and the demo plays: the display folds down
    // and up.
    press(&mut m, "EJECT", 200);
    assert_eq!(m.run(2_500 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    assert!(m.board.display_pos <= 94, "{}: the display is at {} in the demo", name, m.board.display_pos);
    press(&mut m, "EJECT", 200);
    assert_eq!(m.run(2_500 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    assert!(m.board.display_pos >= 894, "{}: the display is at {}", name, m.board.display_pos);

    // The menu (SETUP), Single Player, New Game: the start map.  Then
    // forward and a turn.
    let before = console(&m).len();
    press(&mut m, "SETUP", 150);
    assert_eq!(m.run(800 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    shot(&m, "menu");
    for _ in 0..2 {
        press(&mut m, "SELECT", 150);
        assert_eq!(m.run(800 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    }
    wait_for(&mut m, before, "quake: playing start", 60).unwrap_or_else(|| { shot(&m, "fail"); panic!("{}: no game began: {:?}", name, tail(&m)) });
    assert_eq!(m.run(3_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    shot(&m, "start");
    let (_, standing) = window(&m);
    press(&mut m, "NEXT", 1_000);
    assert!(m.panel_knob("main", 4));
    assert_eq!(m.run(500 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    shot(&m, "moved");
    let (_, moved) = window(&m);
    assert_ne!(standing, moved, "{}: the view did not change with NEXT and the knob", name);

    // While it plays: the disc comes out; a short RETURN is Quake's (the
    // menu), not the way back.
    let ejects = m.atapi().ejects;
    press(&mut m, "CD_EJECT", 200);
    assert_eq!(m.run(2_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    assert_eq!(m.atapi().ejects, ejects + 1, "{}: no eject in the game", name);
    let before = console(&m).len();
    press(&mut m, "RETURN", 300);
    assert_eq!(m.run(1_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    assert!(!console(&m)[before..].contains("rnse sdk: sdboot"), "{}: a short RETURN left the game", name);

    // RETURN held: back to sdboot, with the status of a RETURN held.
    let before = console(&m).len();
    press(&mut m, "RETURN", 2_500);
    assert_eq!(m.run(3_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    let text = console(&m);
    assert!(text[before..].contains("rnse sdk: sdboot") && text[before..].contains("TEST ENDED BY HOLDING RETURN"), "{}: {:?}", name, &text[before..]);
    assert!(!text[before..].lines().any(|l| l.trim_end() == "rnse sdk: quake"), "{}: Quake was started again", name);

    // (A game built with -DRNSE_DEBUG_IDLE says how long the faceplate
    // went unanswered at most.)
    for line in console(&m).lines().filter(|l| l.starts_with("idle:")) {
        eprintln!("{}: {}", name, line);
    }

    // The watchdog was served throughout, card reads included (it resets
    // the unit 105 ms after the last edge), and the caches never handed
    // out anything stale.
    let longest = m.watchdog.longest / ms;
    assert!(longest < 40, "{}: the watchdog waited {} ms, until {} ms, PC {:08x}", name, longest, m.watchdog.longest_end / ms, m.watchdog.longest_pc);
    let cache = &m.cpu.cache.trouble;
    assert_eq!((cache.stale_fetches, cache.dirty_discarded), (0, 0), "{}: {:?}", name, cache);
}

/// The DVD player (`sdk/examples/dvd`) as a unit starts it: the stock
/// software starts the SD loader from the disc, the loader the player
/// from the card (DVD.BIN), CD EJECT takes the loader's disc out (the
/// player says it is no DVD-Video disc), a DVD goes in, SELECT plays
/// its first title with the caches modelled, and RETURN held goes back
/// to the loader.  Needs `RNSE_DVD` as well (`sdk/tests/check-dvd.sh`
/// makes one, and `tests/dvd.rs` has the player's own tests, which need
/// no firmware).
#[test]
fn dvd_player_from_the_sd_loader() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    let Some(dvd) = std::env::var_os("RNSE_DVD") else { return };
    let dvd = std::fs::read(dvd).expect("RNSE_DVD is not readable");
    let sdk = kit();
    let Some((mut m, name, _)) = started_by_stock_software(&image, Model::Rnse192, "sdboot") else { return };
    if sdk_example(&image, Model::Rnse192, &own("dvd/dvd")).is_none() { return }
    let player = std::fs::read(sdk.join("out/dvd-192.bin")).unwrap();
    let flash = m.board.flash.clone();
    let ms = m.cycles_per_ms();
    m.cpu.breakpoints.clear();
    assert_eq!(m.run(2000 * ms), Stop::Budget);
    let card = rnse::fat::card("DVD", &[rnse::fat::Entry::file("DVD.BIN", player)], None).unwrap();
    let at = m.console.len();
    assert!(m.insert_card(0, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
    let started = until(&mut m, at, "rnse sdk: dvd", 30).unwrap_or_else(|| panic!("{}: {}", name, String::from_utf8_lossy(&m.console[at..])));
    until(&mut m, started, "NOT A DVD-VIDEO DISC", 30).unwrap_or_else(|| panic!("{}: {}", name, String::from_utf8_lossy(&m.console[started..])));
    // The loader's disc out, and the DVD in.
    assert!(m.panel_key("CD_EJECT", true));
    assert_eq!(m.run(300 * ms), Stop::Budget);
    assert!(m.panel_key("CD_EJECT", false));
    assert_eq!(m.run(8000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    assert!(m.atapi().image.is_none(), "{}: the loader's disc was not ejected", name);
    let at = m.console.len();
    m.atapi().insert(Some(dvd));
    until(&mut m, at, "READING THE DISC", 40).unwrap_or_else(|| panic!("{}: {}", name, String::from_utf8_lossy(&m.console[at..])));
    assert_eq!(m.run(3000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    assert!(m.panel_key("SELECT", true));
    assert_eq!(m.run(200 * ms), Stop::Budget);
    assert!(m.panel_key("SELECT", false));
    assert_eq!(m.run(5000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    let (_, _, rgb) = m.render();
    let colour = rgb.chunks(3).filter(|p| {
        let (r, g, b) = (p[0] as i32, p[1] as i32, p[2] as i32);
        (r - g).abs() > 40 || (g - b).abs() > 40 || (r - b).abs() > 40
    }).count();
    let text = String::from_utf8_lossy(&m.console[at..]).into_owned();
    assert!(colour > 20_000, "{}: {} colourful pixels: {}", name, colour, text);
    assert!(text.contains("fps*10"), "{}: {}", name, text);
    // RETURN held: the player stops its sound and puts the caches back,
    // and the loader is there again.
    let at = m.console.len();
    assert!(m.panel_key("RETURN", true));
    assert_eq!(m.run(2600 * ms), Stop::Budget);
    assert!(m.panel_key("RETURN", false));
    until(&mut m, at, "TEST ENDED BY HOLDING RETURN", 30).unwrap_or_else(|| panic!("{}: {}", name, String::from_utf8_lossy(&m.console[at..])));
    let text = String::from_utf8_lossy(&m.console[started..]).into_owned();
    assert!(!text.contains("unexpected"), "{}: {}", name, text);
    assert_eq!(m.board.flash, flash, "{}", name);
    let cache = &m.cpu.cache.trouble;
    assert_eq!((cache.stale_fetches, cache.dirty_discarded), (0, 0), "{}: {:?}", name, cache);
}

/// Generated homebrew cartridge: colored background, pulse tone, no copyrighted ROM.
#[test]
fn nes_bare_metal_sd_video_audio() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    let sdk = kit();
    assert!(
        sdk.join("out/nes/NESCORE.BIN").exists(),
        "run nes/build.sh first"
    );
    let (mut m, _, _) = sdk_example(&image, Model::Rnse192, "sdboot")
        .expect("SH4 compiler and 192 firmware required");
    let variants = fw::load_variants(&image).unwrap();
    let v = variants
        .iter()
        .find(|v| v.name == "V222")
        .expect("V222 firmware required");
    m.install(v, &fw::IndexInfo::from_disc(&image, "V222").unwrap());
    m.cpu.breakpoints.clear();
    let mut rom = vec![0; 16 + 16384 + 8192];
    rom[..6].copy_from_slice(&[b'N', b'E', b'S', 26, 1, 1]);
    let program: &[u8] = &[
        0x78, 0xd8, 0xa2, 0xff, 0x9a, // SEI, CLD, stack
        0xa9, 0x00, 0x8d, 0x00, 0x20, 0x8d, 0x01, 0x20, 0xa9, 0x3f, 0x8d, 0x06, 0x20, 0xa9, 0x00,
        0x8d, 0x06, 0x20, 0xa9, 0x21, 0x8d, 0x07, 0x20, // blue universal background
        0xa9, 0x08, 0x8d, 0x01, 0x20, 0xa9, 0x01, 0x8d, 0x15, 0x40, 0xa9, 0xbf, 0x8d, 0x00, 0x40,
        0xa9, 0xfd, 0x8d, 0x02, 0x40, 0xa9, 0x08, 0x8d, 0x03, 0x40, 0x4c, 0x35, 0x80,
    ];
    rom[16..16 + program.len()].copy_from_slice(program);
    for offset in [0x3ffa, 0x3ffc, 0x3ffe] {
        rom[16 + offset] = 0;
        rom[16 + offset + 1] = 0x80;
    }
    let card = rnse::fat::card(
        "NES",
        &[
            rnse::fat::Entry::file(
                "NESCORE.BIN",
                std::fs::read(sdk.join("out/nes/NESCORE.BIN")).unwrap(),
            ),
            rnse::fat::Entry::file(
                "RNSE.BIN",
                std::fs::read(sdk.join("out/nes/RNSE.BIN")).unwrap(),
            ),
            rnse::fat::Entry::file("DEMO.NES", rom),
        ],
        None,
    )
    .unwrap();
    assert!(m.insert_card(0, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
    let flash = m.board.flash.clone();
    m.speaker = Some(Vec::new());
    until(&mut m, 0, "DEMO", 60)
        .unwrap_or_else(|| panic!("{}", String::from_utf8_lossy(&m.console)));
    let ms = m.cycles_per_ms();
    m.panel_key("SELECT", true);
    assert_eq!(m.run(200 * ms), Stop::Budget);
    m.panel_key("SELECT", false);
    until(&mut m, 0, "NES PLAYING", 60)
        .unwrap_or_else(|| panic!("{:?}: {}", m.cpu.fault, String::from_utf8_lossy(&m.console)));
    assert_eq!(m.run(1000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    assert_eq!(m.board.flash, flash);
    assert_eq!(m.cpu.cache.trouble.stale_fetches, 0);
    assert_eq!(m.cpu.cache.trouble.dirty_discarded, 0);
    assert!(!String::from_utf8_lossy(&m.console).contains("unexpected"));
    let picture = m.render();
    let center = (117 * picture.0 + 200) * 3;
    assert!(
        picture.2[center + 2] > picture.2[center],
        "blue cartridge background"
    );
    std::fs::create_dir_all(sdk.join("out/nes")).unwrap();
    std::fs::write(
        sdk.join("out/nes/demo.png"),
        rnse::png::encode_rgb(picture.0, picture.1, &picture.2),
    )
    .unwrap();
    let heard = m.speaker.take().unwrap();
    assert!(
        heard.len() > 44100 && heard.iter().any(|v| v.unsigned_abs() > 1),
        "cartridge pulse audio: {} samples, peak {}", heard.len(), heard.iter().map(|v|v.unsigned_abs()).max().unwrap_or(0)
    );
    let symbols = std::fs::read_to_string(sdk.join("out/nes/nes.sym")).unwrap();
    let inputs = u32::from_str_radix(
        symbols
            .lines()
            .find(|l| l.ends_with(" inputs"))
            .unwrap()
            .split_whitespace()
            .next()
            .unwrap(),
        16,
    )
    .unwrap();
    // With no adapter, faceplate holds become player-one buttons.
    m.panel_key("SOFT_TR", true);
    m.panel_key("NEXT", true);
    m.panel_key("INFO", true);
    assert_eq!(m.run(100 * ms), Stop::Budget);
    assert_eq!(m.peek32(inputs + 4), Some(0x85));
    assert_eq!(m.peek32(inputs + 12), Some(0));
    m.can_frame(rnse::dev::hcan::Frame::new(
        0x6e3,
        &[1, 0, 1, 128, 0, 0, 0, 0],
    ));
    assert_eq!(m.run(80 * ms), Stop::Budget);
    assert_eq!(m.peek32(inputs + 4), Some(1));
    assert_eq!(m.peek32(inputs + 12), Some(128));
    assert_eq!(m.run(250 * ms), Stop::Budget);
    assert_eq!(m.peek32(inputs + 4), Some(0x85)); // held faceplate resumes on timeout
    assert_eq!(m.peek32(inputs + 12), Some(0));
    m.panel_key("SOFT_TR", false);
    m.panel_key("NEXT", false);
    m.panel_key("INFO", false);
    assert_eq!(m.run(100 * ms), Stop::Budget);
    assert_eq!(m.peek32(inputs + 4), Some(0));
    let at = m.console.len();
    m.panel_key("RETURN", true);
    assert_eq!(m.run(200 * ms), Stop::Budget);
    m.panel_key("RETURN", false);
    until(&mut m, at, "DEMO", 30).expect("return to ROM browser restores SD");
    m.panel_key("SELECT", true);
    assert_eq!(m.run(200 * ms), Stop::Budget);
    m.panel_key("SELECT", false);
    until(&mut m, at, "NES PLAYING", 30).expect("second game launch");
    assert_eq!(m.board.flash, flash);
    m.panel_key("RETURN", true);
    assert_eq!(m.run(2500 * ms), Stop::Budget);
    m.panel_key("RETURN", false);
    until(&mut m, at, "TEST ENDED BY HOLDING RETURN", 30)
        .unwrap_or_else(||panic!("long RETURN: {}",String::from_utf8_lossy(&m.console[at..])));
    assert_eq!(m.board.flash, flash);
    assert_eq!(m.cpu.cache.trouble.stale_fetches, 0);
    assert_eq!(m.cpu.cache.trouble.dirty_discarded, 0);
}

/// Optional user-provided cartridge smoke test; never stores the ROM in the repo.
#[test]
fn nes_user_rom_from_sd_loader() {
    use rnse::board::Model;
    let Some(path) = std::env::var_os("RNSE_NES_ROM") else {
        return;
    };
    let image = disc().expect("RNSE_DISC required for user-ROM test");
    let sdk = kit();
    let rom = std::fs::read(path).expect("RNSE_NES_ROM unreadable");
    assert!(rom.starts_with(b"NES\x1a"));
    let (mut m, _, _) =
        sdk_example(&image, Model::Rnse192, "sdboot").expect("SH4 compiler required");
    let variants = fw::load_variants(&image).unwrap();
    let v = variants
        .iter()
        .find(|v| v.name == "V222")
        .expect("V222 required");
    m.install(v, &fw::IndexInfo::from_disc(&image, "V222").unwrap());
    m.cpu.breakpoints.clear();
    let card = rnse::fat::card(
        "NES",
        &[
            rnse::fat::Entry::file(
                "NESCORE.BIN",
                std::fs::read(sdk.join("out/nes/NESCORE.BIN")).unwrap(),
            ),
            rnse::fat::Entry::file(
                "NES.BIN",
                std::fs::read(sdk.join("out/nes/RNSE.BIN")).unwrap(),
            ),
            rnse::fat::Entry::file("METROID.NES", rom),
        ],
        None,
    )
    .unwrap();
    assert!(m.insert_card(0, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
    let flash = m.board.flash.clone();
    until(&mut m, 0, "METROID", 60)
        .unwrap_or_else(|| panic!("{}", String::from_utf8_lossy(&m.console)));
    let ms = m.cycles_per_ms();
    m.panel_key("SELECT", true);
    assert_eq!(m.run(200 * ms), Stop::Budget);
    m.panel_key("SELECT", false);
    until(&mut m, 0, "NES PLAYING", 30)
        .unwrap_or_else(|| panic!("{}", String::from_utf8_lossy(&m.console)));
    assert_eq!(m.run(5000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    let save = |m: &Machine, name: &str| {
        let (w, h, p) = m.render();
        std::fs::write(
            sdk.join("out/nes").join(name),
            rnse::png::encode_rgb(w, h, &p),
        )
        .unwrap();
        assert!(
            p.chunks_exact(3)
                .any(|v| v[0] > 60 || v[1] > 60 || v[2] > 60),
            "visible cartridge output"
        );
    };
    save(&m, "metroid-title.png");
    // Faceplate Start, then A + Right, with no CAN adapter attached.
    m.panel_key("SELECT", true);
    assert_eq!(m.run(200 * ms), Stop::Budget);
    m.panel_key("SELECT", false);
    assert_eq!(m.run(5000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    save(&m, "metroid-start.png");
    m.panel_key("SELECT", true);
    assert_eq!(m.run(200 * ms), Stop::Budget);
    m.panel_key("SELECT", false);
    assert_eq!(m.run(5000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    save(&m, "metroid-gameplay.png");
    m.panel_key("SOFT_TR", true);
    m.panel_key("NEXT", true);
    assert_eq!(m.run(1000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    m.panel_key("SOFT_TR", false);
    m.panel_key("NEXT", false);
    save(&m, "metroid-controls.png");
    assert_eq!(m.board.flash, flash);
    assert_eq!(m.cpu.cache.trouble.stale_fetches, 0);
    assert_eq!(m.cpu.cache.trouble.dirty_discarded, 0);
    assert!(!String::from_utf8_lossy(&m.console).contains("unexpected"));
}
