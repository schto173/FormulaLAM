use rppal::gpio::{Gpio, Trigger};
use std::time::{Instant, Duration};
use std::error::Error;
use serde_json::json;
use std::sync::atomic::{Ordering, AtomicBool};
use std::sync::{Arc, mpsc};
use signal_hook::{consts::SIGINT, iterator::Signals};
use std::process::Command;
use std::fs::OpenOptions;
use std::io::Write;

const GPIO_PIN: u8 = 26;
const TIMEOUT_SECS: u64 = 2;
const STATUS_FILE: &str = "/tmp/wheel_speed.json";
const MAX_RPM: f64 = 550.0;  // ~45 km/h with 1.5m circumference
const MIN_RPM: f64 = 5.0;    // Minimum RPM to consider valid
// Theoretical minimum time between pulses at MAX_RPM
const MIN_PULSE_TIME_MS: u64 = ((1000.0 * 60.0) / MAX_RPM) as u64;

// Acceleration limiting constants
const MAX_RPM_INCREASE: f64 = 120.0; // 6 km/h with 1.5m circumference = (6 * 1000) / (1.5 * 60) RPM
const RPM_INCREMENT: f64 = 12.2;    // 1 km/h worth of RPM = (1 * 1000) / (1.5 * 60) RPM

fn write_status_nonblocking(status: serde_json::Value) -> Result<(), Box<dyn Error>> {
    let file = OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open(STATUS_FILE)?;
    
    let mut writer = std::io::BufWriter::new(file);
    serde_json::to_writer(&mut writer, &status)?;
    writer.flush()?;
    Ok(())
}

fn cleanup_gpio(pin: u8) {
    Command::new("sh")
        .arg("-c")
        .arg(format!("echo {} > /sys/class/gpio/unexport 2>/dev/null || true", pin))
        .output()
        .ok();
}

fn main() -> Result<(), Box<dyn Error>> {
    println!("Initializing GPIO...");
    cleanup_gpio(GPIO_PIN);
    std::thread::sleep(Duration::from_millis(100));
    
    let gpio = Gpio::new()?;
    let mut pin = gpio.get(GPIO_PIN)?.into_input_pullup();
    
    let (tx, rx) = mpsc::channel();
    
    // Spawn file writer thread
    std::thread::spawn(move || {
        while let Ok(status) = rx.recv() {
            if let Err(e) = write_status_nonblocking(status) {
                eprintln!("Failed to write status: {}", e);
            }
        }
    });
    
    let mut last_time = Instant::now();
    let mut counter = 0;
    let mut current_rpm = 0.0;
    let mut last_valid_rpm = 0.0;
    let mut spike_rejected_count = 0;
    
    println!("Monitoring wheel sensor on GPIO {}...", GPIO_PIN);
    println!("Maximum speed: {:.1} RPM (~45 km/h)", MAX_RPM);
    println!("Max RPM increase allowed: {:.1} RPM (~6 km/h)", MAX_RPM_INCREASE);
    println!("Press Ctrl+C to exit");

    // No debounce time set - we'll validate timing in software
    pin.set_interrupt(Trigger::FallingEdge, None)?;

    // Handle Ctrl+C
    let mut signals = Signals::new(&[SIGINT])?;
    let running = Arc::new(AtomicBool::new(true));
    let running_clone = running.clone();

    std::thread::spawn(move || {
        for _ in signals.forever() {
            running_clone.store(false, Ordering::SeqCst);
            break;
        }
    });

    // Initialize status file
    tx.send(json!({
        "rpm": 0.0,
        "count": 0,
        "timestamp": Instant::now().elapsed().as_secs(),
        "running": true
    }))?;

    while running.load(Ordering::SeqCst) {
        if pin.poll_interrupt(false, Some(Duration::from_millis(100)))?.is_some() {
            let now = Instant::now();
            let duration = now.duration_since(last_time);
            let duration_ms = duration.as_millis() as u64;

            // Skip if pulse came too fast (physically impossible)
            if duration_ms < MIN_PULSE_TIME_MS {
                continue;
            }

            let instant_rpm = 60.0 / duration.as_secs_f64();
            
            // Validate RPM is within reasonable bounds
            if instant_rpm <= MAX_RPM && instant_rpm >= MIN_RPM {
                // Check for unrealistic acceleration (only for increases)
                let rpm_increase = instant_rpm - last_valid_rpm;
                
                if rpm_increase > MAX_RPM_INCREASE {
                    // Spike detected - use last valid RPM + 1 km/h worth of RPM
                    current_rpm = last_valid_rpm + RPM_INCREMENT;
                    spike_rejected_count += 1;
                    
                    println!("Spike rejected! Would have been {:.1} RPM, using {:.1} RPM instead",
                             instant_rpm, current_rpm);
                    println!("Total spikes rejected: {}", spike_rejected_count);
                } else {
                    // Valid reading
                    current_rpm = instant_rpm;
                    last_valid_rpm = current_rpm;
                }
                
                counter += 1;
                println!("Rotation {}: {:.1} RPM", counter, current_rpm);
                
                tx.send(json!({
                    "rpm": current_rpm,
                    "count": counter,
                    "timestamp": now.elapsed().as_secs(),
                    "running": true,
                    "spikes_rejected": spike_rejected_count
                }))?;
            }
            
            last_time = now;
        } else if current_rpm != 0.0 && last_time.elapsed() >= Duration::from_secs(TIMEOUT_SECS) {
            current_rpm = 0.0;
            last_valid_rpm = 0.0; // Reset when stopped
            println!("Speed: 0.0 RPM (no rotation for {} seconds)", TIMEOUT_SECS);
            
            tx.send(json!({
                "rpm": 0.0,
                "count": counter,
                "timestamp": Instant::now().elapsed().as_secs(),
                "running": true,
                "spikes_rejected": spike_rejected_count
            }))?;
        }
    }

    // Final update
    tx.send(json!({
        "rpm": current_rpm,
        "count": counter,
        "timestamp": Instant::now().elapsed().as_secs(),
        "running": false,
        "spikes_rejected": spike_rejected_count
    }))?;

    cleanup_gpio(GPIO_PIN);
    Ok(())
}
