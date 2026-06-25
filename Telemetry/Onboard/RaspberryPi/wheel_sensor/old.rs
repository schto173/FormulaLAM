use rppal::gpio::{Gpio, Trigger};
use std::time::{Instant, Duration};
use std::error::Error;
use std::fs;
use serde_json::{json, to_string};
use std::sync::atomic::Ordering;
use std::sync::atomic::AtomicBool;
use std::sync::Arc;
use signal_hook::{consts::SIGINT, iterator::Signals};
use std::process::Command;

const GPIO_PIN: u8 = 17;
const TIMEOUT_SECS: u64 = 2;     // 2 seconds timeout
const DEBOUNCE_MS: u64 = 20;     // 60 milliseconds debounce
const STATUS_FILE: &str = "/tmp/wheel_speed.json";

fn cleanup_gpio(pin: u8) {
    // Try to unexport the GPIO pin using sysfs
    Command::new("sh")
        .arg("-c")
        .arg(format!("echo {} > /sys/class/gpio/unexport 2>/dev/null || true", pin))
        .output()
        .ok();
}

fn main() -> Result<(), Box<dyn Error>> {
    println!("Initializing GPIO...");
    
    // Cleanup GPIO first
    cleanup_gpio(GPIO_PIN);
    
    // Small delay to allow system to cleanup
    std::thread::sleep(Duration::from_millis(100));
    
    let gpio = Gpio::new()?;
    let mut pin = match gpio.get(GPIO_PIN) {
        Ok(p) => p.into_input_pullup(),
        Err(e) => {
            eprintln!("Failed to access GPIO {}: {}", GPIO_PIN, e);
            cleanup_gpio(GPIO_PIN);
            return Err(e.into());
        }
    };
    
    let mut last_time = Instant::now();
    let mut counter = 0;
    let mut current_rpm = 0.0;
    
    println!("Monitoring wheel sensor on GPIO {}...", GPIO_PIN);
    println!("Debounce time: {}ms", DEBOUNCE_MS);
    println!("Press Ctrl+C to exit");

    // Set interrupt with debounce time
    match pin.set_interrupt(Trigger::FallingEdge, Some(Duration::from_millis(DEBOUNCE_MS))) {
        Ok(_) => (),
        Err(e) => {
            eprintln!("Failed to set interrupt: {}", e);
            cleanup_gpio(GPIO_PIN);
            return Err(e.into());
        }
    }

    // Handle Ctrl+C gracefully
    let mut signals = Signals::new(&[SIGINT])?;
    let running = Arc::new(AtomicBool::new(true));
    let running_clone = running.clone();

    // Spawn signal handler
    std::thread::spawn(move || {
        for _ in signals.forever() {
            running_clone.store(false, Ordering::SeqCst);
            break;
        }
    });

    // Initialize status file
    let status = json!({
        "rpm": 0.0,
        "count": 0,
        "timestamp": Instant::now().elapsed().as_secs(),
        "running": true
    });
    fs::write(STATUS_FILE, to_string(&status)?)?;

    while running.load(Ordering::SeqCst) {
        if pin.poll_interrupt(false, Some(Duration::from_millis(2000)))?.is_some() {
            // Interrupt occurred
            let now = Instant::now();
            let duration = now.duration_since(last_time);
            current_rpm = 60.0 / duration.as_secs_f64();
            
            counter += 1;
            println!("Rotation {}: {:.1} RPM", counter, current_rpm);
            
            // Update status file
            let status = json!({
                "rpm": current_rpm,
                "count": counter,
                "timestamp": now.elapsed().as_secs(),
                "running": true
            });
            fs::write(STATUS_FILE, to_string(&status)?)?;
            
            last_time = now;
        } else {
            if current_rpm != 0.0 {
        current_rpm = 0.0;
        println!("Speed: 0.0 RPM (no rotation for {} seconds)", TIMEOUT_SECS);
        
        // Update status file with zero speed
        let status = json!({
            "rpm": 0.0,
            "count": counter,
            "timestamp": Instant::now().elapsed().as_secs(),
            "running": true
        });
        fs::write(STATUS_FILE, to_string(&status)?)?;
    }
        }
    }

    // Final update with running = false
    let status = json!({
        "rpm": current_rpm,
        "count": counter,
        "timestamp": Instant::now().elapsed().as_secs(),
        "running": false
    });
    fs::write(STATUS_FILE, to_string(&status)?)?;

    // Cleanup GPIO on exit
    cleanup_gpio(GPIO_PIN);

    Ok(())
}
