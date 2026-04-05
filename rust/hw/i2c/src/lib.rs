// SPDX-License-Identifier: GPL-2.0-or-later

//! I2C bus model for G233 SoC (QEMU Camp 2026 experiment).
//!
//! Students must implement the trait methods and bus logic.
//! The stubs compile but return default values; unit tests
//! will fail until the real logic is filled in.

pub mod bus;

/// Trait representing a single device on the I2C bus.
pub trait I2CDevice {
    /// Return the 7-bit I2C address of this device.
    fn address(&self) -> u8;

    /// Read one byte from the given register.
    fn read(&mut self, reg: u8) -> u8;

    /// Write one byte to the given register.
    fn write(&mut self, reg: u8, data: u8);
}

/// A simple I2C bus that holds a collection of devices.
pub struct I2CBus {
    devices: Vec<Box<dyn I2CDevice>>,
}

impl I2CBus {
    /// Create an empty bus with no attached devices.
    pub fn new() -> Self {
        // TODO: implement
        Self {
            devices: Vec::new(),
        }
    }

    /// Attach a device to the bus.
    pub fn attach(&mut self, _device: Box<dyn I2CDevice>) {
        // TODO: implement — push the device onto the bus
    }

    /// Return the number of devices currently on the bus.
    pub fn device_count(&self) -> usize {
        // TODO: implement
        0
    }

    /// Perform a read transfer: find the device at `addr`, read
    /// register `reg`, and return the value.  Returns `None` if
    /// no device with that address is attached (NACK).
    pub fn transfer_read(&mut self, _addr: u8, _reg: u8) -> Option<u8> {
        // TODO: implement — iterate devices, match by address,
        //       call read(), return Some(value)
        None
    }

    /// Perform a write transfer: find the device at `addr` and
    /// write `data` to register `reg`.  Returns `true` on ACK
    /// (device found), `false` on NACK.
    pub fn transfer_write(&mut self, _addr: u8, _reg: u8, _data: u8) -> bool {
        // TODO: implement — iterate devices, match by address,
        //       call write(), return true
        false
    }
}

// ---------------------------------------------------------------------------
// Unit tests — these intentionally FAIL against the stubs above.
// Students must fill in the TODO bodies to make them pass.
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    /// A trivial mock device that always returns a fixed value.
    struct MockDevice {
        addr: u8,
    }

    impl I2CDevice for MockDevice {
        fn address(&self) -> u8 {
            self.addr
        }
        fn read(&mut self, _reg: u8) -> u8 {
            0xAB
        }
        fn write(&mut self, _reg: u8, _data: u8) {}
    }

    /// A mock EEPROM: 256-byte register file.
    struct MockEeprom {
        addr: u8,
        regs: [u8; 256],
    }

    impl MockEeprom {
        fn new(addr: u8) -> Self {
            Self {
                addr,
                regs: [0u8; 256],
            }
        }
    }

    impl I2CDevice for MockEeprom {
        fn address(&self) -> u8 {
            self.addr
        }
        fn read(&mut self, reg: u8) -> u8 {
            self.regs[reg as usize]
        }
        fn write(&mut self, reg: u8, data: u8) {
            self.regs[reg as usize] = data;
        }
    }

    #[test]
    fn test_i2c_bus_create() {
        let mut bus = I2CBus::new();
        bus.attach(Box::new(MockDevice { addr: 0x50 }));
        assert_eq!(bus.device_count(), 1, "bus should have exactly 1 device");
    }

    #[test]
    fn test_i2c_bus_read_write() {
        let mut bus = I2CBus::new();
        bus.attach(Box::new(MockEeprom::new(0x50)));

        let ack = bus.transfer_write(0x50, 0x10, 0xDE);
        assert!(ack, "write to attached device must ACK");

        let val = bus.transfer_read(0x50, 0x10);
        assert_eq!(val, Some(0xDE), "read back must return written value");
    }

    #[test]
    fn test_i2c_bus_nack() {
        let mut bus = I2CBus::new();
        bus.attach(Box::new(MockDevice { addr: 0x50 }));

        let val = bus.transfer_read(0x77, 0x00);
        assert_eq!(val, None, "read from non-existent address must NACK");
    }
}
