#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include <linux/kfifo.h>


#define MY_DEVICE_FILE  "morse-code"

#define DOT_TIME 200

DEFINE_LED_TRIGGER(morse_code);

static DECLARE_KFIFO(morse_code_fifo, char, 512);

static void getMorse(char letter);
static void processMorseSeq(void);
static void flashLed(unsigned short letter);
static int add_to_kfifo(char character);




static unsigned short morsecode_codes[] = {
		0xB800,	// A 1011 1
		0xEA80,	// B 1110 1010 1
		0xEBA0,	// C 1110 1011 101
		0xEA00,	// D 1110 101
		0x8000,	// E 1
		0xAE80,	// F 1010 1110 1
		0xEE80,	// G 1110 1110 1
		0xAA00,	// H 1010 101
		0xA000,	// I 101
		0xBBB8,	// J 1011 1011 1011 1
		0xEB80,	// K 1110 1011 1
		0xBA80,	// L 1011 1010 1
		0xEE00,	// M 1110 111
		0xE800,	// N 1110 1
		0xEEE0,	// O 1110 1110 111
		0xBBA0,	// P 1011 1011 101
		0xEEB8,	// Q 1110 1110 1011 1
		0xBA00,	// R 1011 101
		0xA800,	// S 1010 1
		0xE000,	// T 111
		0xAE00,	// U 1010 111
		0xAB80,	// V 1010 1011 1
		0xBB80,	// W 1011 1011 1
		0xEAE0,	// X 1110 1010 111
		0xEBB8,	// Y 1110 1011 1011 1
		0xEEA0	// Z 1110 1110 101
};

unsigned short morseSeq[512];
int morseSeqPosition = 0;
 // skips adding space if 1 start as 1 to ensure letter is added first, no leading whitspace
int spaceFlag = 1;
int lastLetterPosition = 0;

static void getMorse(char letter){
    int letterValue = letter - 65;
    if(letterValue >= 0 && letterValue <= 25){
        spaceFlag = 0;
        morseSeq[morseSeqPosition] = morsecode_codes[letterValue];
        morseSeqPosition++;
        lastLetterPosition = morseSeqPosition;
    } else if(letter == 32){
        if(spaceFlag == 0){
            morseSeq[morseSeqPosition] = 0x0000;
            morseSeqPosition++;
            spaceFlag = 1;
        }  
    }
}

static void processMorseSeq(){
    int i =0;
    for(i = 0; i < morseSeqPosition -1; i++){
        flashLed(morseSeq[i]);
        add_to_kfifo(' ');
        msleep(DOT_TIME);
    }
    // done seperately to skip pause at end of sequence
    flashLed(morseSeq[morseSeqPosition -1]);
    add_to_kfifo('\n');
}

static void flashLed(unsigned short letter){
    unsigned short dotMask = 0x8000;
    unsigned short dashMask = 0xE000;
    int bitPosition = 0;
    if(letter != 0x0000){
        while(bitPosition < 16){
            if((letter & dashMask) == dashMask){
                led_trigger_event(morse_code, LED_FULL);
                add_to_kfifo('-');
                msleep(DOT_TIME * 3);
                led_trigger_event(morse_code, LED_OFF);
                msleep(DOT_TIME);
                letter = letter << 3;
                bitPosition += 3;
            } else if((letter & dotMask) == dotMask){
                led_trigger_event(morse_code, LED_FULL);
                add_to_kfifo('.');
                msleep(DOT_TIME);
                led_trigger_event(morse_code, LED_OFF);
                msleep(DOT_TIME);
                letter = letter << 1;
                bitPosition += 1;
            } else {
                letter = letter << 1;
                bitPosition += 1;
            }
        }
    } else {
      add_to_kfifo(' ');
        msleep(DOT_TIME);
    }
    
}

static void led_register(void) {
    led_trigger_register_simple("morse-code", &morse_code);
}

static void led_unregister(void){
    led_trigger_unregister_simple(morse_code);
}

static int add_to_kfifo(char character){
    if(!kfifo_put(&morse_code_fifo, character)){
        return -EFAULT;
    }
    return 0;
}

static ssize_t morse_code_read(struct file *file, char *buff, size_t count, loff_t *ppos) {
    int num_of_bytes_read = 0;

    if(kfifo_to_user(&morse_code_fifo, buff, count, &num_of_bytes_read)){
        return -EFAULT;
    }
    
    return num_of_bytes_read;
}

static ssize_t morse_code_write(struct file* file, const char *buff,
		size_t count, loff_t* ppos)
{
	int i;
    morseSeqPosition = 0;

	// Blink once per character (-1 to skip end null)
	for (i = 0; i < count-1; i++) {
        char ch;
        if(copy_from_user(&ch, &buff[i], sizeof(ch))){
            return -EFAULT;
        }
		getMorse(toupper(ch));
	}

    // used to skip any trailing whitespace
    morseSeqPosition = lastLetterPosition;
    processMorseSeq();
	// Return # bytes actually written.
	return count;
}


struct file_operations my_fops = {
	.owner    =  THIS_MODULE,
	.write    =  morse_code_write,
    .read     =  morse_code_read,
};

// Character Device info for the Kernel:
static struct miscdevice my_miscdevice = {
		.minor    = MISC_DYNAMIC_MINOR,         // Let the system assign one.
		.name     = MY_DEVICE_FILE,             // /dev/.... file.
		.fops     = &my_fops                    // Callback functions.
};

// 
static int __init morsedriver_init(void)
{
    int ret;
    printk(KERN_INFO "Morse Writer init()\n");
    ret = misc_register(&my_miscdevice);
    led_register();
    INIT_KFIFO(morse_code_fifo);
	return ret;
}
static void __exit morsedriver_exit(void)
{
printk(KERN_INFO "Morse Writer exit().\n");
led_unregister();
misc_deregister(&my_miscdevice);
}
// Link our init/exit functions into the kernel's code.
module_init(morsedriver_init);
module_exit(morsedriver_exit);
// Information about this module:
MODULE_AUTHOR("ATurner");
MODULE_DESCRIPTION("Morse Code Driver");
MODULE_LICENSE("GPL");// Important to leave as GPL.


