##### STM32F1 CAN bootloader

#### Overview

The CAN ID for all commands is 0xB0 (CANID_BOOTLOADER_CMD) and the bootloader replies to all commands with a status having CAN ID 0xB1 (CANID_BOOTLOADER_RPLY).

**The bootloader implements 3 commands:**
  - **Write page buffer** (BL_CMD_WRITE_BUF) is used to fill the bootloader's page buffer (in RAM) with data
  - **Write page** (BL_CMD_WRITE_PAGE) is used to write the page buffer to flash
  - **Write CRC** (BL_CMD_WRITE_CRC) is used to store entire flash CRC

**All 3 commands have the same format (8 bytes of standard CAN frame are used):**
  - uint8_t **board ID**
  - uint8_t **command**
  - uint16_t **par1**
  - uint32_t **par2**

Each board appearing on the CAN bus should have a unique //board ID//. This assures you're actually talking to the board you want to be talking to.

**Write page buffer:**
  - **offset** (par1), offset into the page buffer
  - **data** (par2), data to write at offset

**Write page:**
  - **page number** (par1), page number to flash with data in page buffer (0..PAGE_COUNT-1)
  - **page CRC** (par2), page buffer CRC, if not matching, bootloader will not flash the page

**Write CRC:**
  - **page count** (par1), number of pages the firmware uses
  - **firmware CRC** (par2), entire firmware CRC, if not matching the flash contents, bootloader will not flash firmware CRC

Page count and firmware CRC are stored in bootloader's persistent storage (an unused flash page at the end of bootloader code area). The bootloader will not execute the firmware if the CRC calculated from flash contents does not match the stored CRC.

#### Programming

**To program the application firmware:**
  - Fill the entire page buffer 4 bytes at a time using several //Write page buffer// commands.
  - Execute //Write page// command providing the correct page data CRC. The bootloader will compare your CRC to the CRC of its page buffer. If they match, it will flash the page.
  - Repeat above steps for all pages.
  - Finally execute //Write CRC// command providing the correct CRC for entire firmware. The bootloader will compare your CRC to the CRC of the MCU's flash. If they match, it will store the CRC in an unused page, allowing subsequent application execution.

The entire procedure is implemented in **prg.py**. The script is written for PEAK System Ethernet to CAN gateway IPEH-004010, using its UDP-CAN translation capabilities. It will have to be adapted for other means of communicating with CAN devices (SocketCAN, SLCAN, ...).

#### How the bootloader works

When a Cortex-M3 CPU resets it loads the word at address 0x0 into SP and the next word (address 0x4) into PC. There is usually ROM at that address containing the manufacturer's bootloader. On STM32 CPUs, the application flash is mapped to 0x08000000 so the manufacturer's bootloader jumps there in a fashion that mimicks CPU reset, i.e. load SP from 0x08000000, load PC from 0x08000004. Instead of a normal application, a secondary "user bootloader" can be executed in its place and the actual application space is agreed upon to be elsewhere (0x08002000 in this case, i.e. the first 8k of flash are used by the bootloader). The bootloader initializes the CAN hardware and waits for CAN messages. If there are no CAN messages for a specified amount of time (NOCANRX_TO, default 5s), the bootloader checks if the stored application flash CRC equals actual flash contents and if so, runs the application.

Running the application is a bit trickier than simply jumping to its entry address. The application programmer rightfully assumes that the CPU was just reset and most of the peripherals are in a well known state (as specified in the datasheet). Because the bootloader was using some peripherals to communicate with the outside world, this is no longer the case. There are two methods (that I know of) of solving this issue:
  - undo the changes to peripheral registers and jump to application
  - reset the CPU and jump to application early, before initializing any peripherals
The second method is implemented by this bootloader. The problem now becomes how to tell the bootloader that was just reset to jump to application code instead of proceeding normally. One approach is to have the bootloader sample a pin. This approach is often used by the manufacturer's bootloader. There's a downside however - starting the bootloader requires physical access to the board making remote updates impractical. Another approach is to leave a message in a bottle somewhere where it will survive a CPU reset. The most obvious way is to write a magic number to RAM since its contents are usually not explicitly zeroed or modified much between reset and very early boot. This is the approach used here. A RAM address outside bootloader's variable space is set to a magic value. Very early in bootloader execution, this RAM location is checked and if the correct magic value is found, a jump to the application code is made instead of proceeding with bootloader initialization and operation. To facilitate this process, the bootloader uses a modified startup assembly code.
```
/* Bootloader support */
  bl PreSystemInit
/* Call the clock system initialization function.*/
  bl  SystemInit
/* Call static constructors */
  bl __libc_init_array
/* Call the application's entry point.*/
  bl	main
  bx	lr
```
The added call to PreSystemInit gives the bootloader a chance to execute some code before even SystemInit is called. As mentioned, the magic value has to be written outside bootloader's variable space. This is because these sections get initialized (.data) or zeroed (.bss) by startup code and any magic value written there would get overwritten.

#### Changes to the application due to bootloader

Since the bootloader is now occupying the space where the application would normally be, the actual application needs to be changed to reflect this fact. The application developer needs to do two (or rather three) things:
  - Tell the linker the new address where the application resides.
  - Relocate the interrupt vector table to the new address.
  - To enable remote updates, provide a means for application to reset the CPU and thus reenter bootloader code.

For the first, find the flash address definition in the linker script. For STM32 micros, the relevant code will usually look
like this:
```
MEMORY
{
RAM (xrw)      : ORIGIN = 0x20000000, LENGTH = 20K
FLASH (rx)     : ORIGIN = 0x08000000, LENGTH = 64K
}
</code>
Change flash to:
<code>
FLASH (rx)     : ORIGIN = 0x08002000, LENGTH = 56K
```
Secondly, relocate the vector table immediately upon entering main like this:
```
int main(void)
{
	// relocate vector table, MUY IMPORTANTE!
	SCB->VTOR = (uint32_t)0x08002000;
  .
  .
  .
```
There seem to be some provisions for this in ST's SystemInit() with the variable VECT_TAB_OFFSET, but instead of letting the user define it with compiler options, it is just fixed to 0, making it unusable without explicitly changing the source. Perhaps this has been fixed in newer versions. Anyway, the above method always works.
