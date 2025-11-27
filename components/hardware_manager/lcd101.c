#include "lcd101.h"
#include "hardware_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

#include "esp_private/spi_common_internal.h"
#include "soc/spi_periph.h"
#include "hal/spi_hal.h"


#include "soc/spi_reg.h"
#include "soc/spi_struct.h"
#include "soc/gpio_struct.h"

#include "driver/gpio.h"
#include "esp_private/gpio.h"  // برای gpio_matrix_out

static const char *TAG = "lcd101";




// تعریف پین‌ها از کانفیگ
#define PIN_NUM_MISO LCD_PIN_MISO
#define PIN_NUM_MOSI LCD_PIN_MOSI
#define PIN_NUM_CLK  LCD_PIN_CLK
#define PIN_NUM_CS   LCD_PIN_CS
#define GPIO_CE      LCD_PIN_CS  // پین DC/Command


// Philips PCF8833 LCD controller command codes
#define NOP 0x00 // nop
#define SWRESET 0x01 // software reset
#define BSTROFF 0x02 // booster voltage OFF
#define BSTRON 0x03 // booster voltage ON
#define RDDIDIF 0x04 // read display identification
#define RDDST 0x09 // read display status
#define SLEEPIN 0x10 // sleep in
#define SLEEPOUT 0x11 // sleep out
#define PTLON 0x12 // partial display mode
#define NORON 0x13 // display normal mode
#define INVOFF 0x20 // inversion OFF
#define INVON 0x21 // inversion ON
#define DALO 0x22 // all pixel OFF
#define DAL 0x23 // all pixel ON
#define SETCON 0x25 // write contrast
#define DISPOFF 0x28 // display OFF
#define DISPON 0x29 // display ON
#define CASET 0x2A // column address set
#define PASET 0x2B // page address set
#define RAMWR 0x2C // memory write
#define RGBSET 0x2D // colour set
#define RGBSET8     0xce
#define PTLAR 0x30 // partial area
#define VSCRDEF 0x33 // vertical scrolling definition
#define TEOFF 0x34 // test mode
#define TEON 0x35 // test mode
#define MADCTL 0x36 // memory access control
#define SEP 0x37 // vertical scrolling start address
#define IDMOFF 0x38 // idle mode OFF
#define IDMON 0x39 // idle mode ON
#define COLMOD 0x3A // interface pixel format
#define SETVOP 0xB0 // set Vop
#define BRS 0xB4 // bottom row swap
#define TRS 0xB6 // top row swap
#define DISCTR 0xB9 // display control
#define DOR 0xBA // data order
#define TCDFE 0xBD // enable/disable DF temperature compensation
#define TCVOPE 0xBF // enable/disable Vop temp comp
#define EC 0xC0 // internal or external oscillator
#define SETMUL 0xC2 // set multiplication factor
#define TCVOPAB 0xC3 // set TCVOP slopes A and B
#define TCVOPCD 0xC4 // set TCVOP slopes c and d
#define TCDF 0xC5 // set divider frequency
#define DF8COLOR 0xC6 // set divider frequency 8-color mode
#define SETBS 0xC7 // set bias system
#define RDTEMP 0xC8 // temperature read back
#define NLI 0xC9 // n-line inversion
#define RDID1 0xDA // read ID1
#define RDID2 0xDB // read ID2
#define RDID3 0xDC // read ID3

// متغیر global برای SPI
static spi_device_handle_t spi_lcd;
static bool spi_initialized = false;

// توابع تاخیر
void Delay_ms(uint16_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void os_delay_us(uint16_t us) {
    esp_rom_delay_us(us);
}

// تابع برای راه‌اندازی SPI با 9-bit mode در IDF 5.x
esp_err_t spi_master_init(void) {
    if (spi_initialized) {
        ESP_LOGI(TAG, "SPI bus already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing SPI master for LCD (9-bit mode)");

    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 16 , // برای 9-bit
    };

    // راه‌اندازی SPI bus برای LCD (HSPI)
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // پیکربندی device برای 9-bit mode
    spi_device_interface_config_t devcfg = {
        .command_bits = 1,  // 1-bit برای command/data
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0,
        .duty_cycle_pos = 0,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = 40 * 1000 * 1000,
        .input_delay_ns = 0,
        .spics_io_num = PIN_NUM_CS,
        .flags = 0,
        .queue_size = 16,
        .pre_cb = NULL,
        .post_cb = NULL,
    };

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi_lcd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return ret;
    }

    // راه‌اندازی GPIOها
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_CE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    spi_initialized = true;
    ESP_LOGI(TAG, "SPI initialized successfully for LCD");
    return ESP_OK;
}

// تابع ارسال داده با 9-bit mode
static void send_9bit(uint8_t data, bool is_data) {
    spi_transaction_ext_t t = {
        .base = {
            .flags = SPI_TRANS_VARIABLE_CMD,
            .cmd = is_data ? 1 : 0,  // 1=data, 0=command
            .addr = 0,
            .length = 8,  // 8 bits data
            .rxlength = 0,
            .tx_buffer = &data,
            .rx_buffer = NULL
        },
        .command_bits = 1
    };

    esp_err_t ret = spi_device_polling_transmit(spi_lcd, (spi_transaction_t*)&t);
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "SPI transmit failed: %s", esp_err_to_name(ret));
    // }
}

#define SPI_NUM 0x02


void lcd_pin_init(void)
{
	int i;
	gpio_num_t pins[] = {
		GPIO_CE,
       // GPIO_DIN,
      //  GPIO_CLK,

	};

	gpio_config_t conf = {
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
	};

	for (i = 0; i < sizeof(pins) / sizeof(gpio_num_t); ++i) {
        conf.pin_bit_mask = 1LL << pins[i];
        gpio_config(&conf);
    }
}

 void spi_master_init1()
{

   esp_err_t ret;
   spi_device_handle_t spi;

   //   adc1_config_width(ADC_WIDTH_12Bit);
    // adc1_config_channel_atten(ADC1_CHANNEL_0,ADC_ATTEN_11db);
    // int val = adc1_get_voltage(ADC1_CHANNEL_0);
   
    spi_device_interface_config_t  devcfg1;
       devcfg1.spics_io_num=PIN_NUM_CS;
       devcfg1.clock_speed_hz=40 * 1000 * 1000;         //Clock out at 40 MHz
       devcfg1.mode=0;                                  //SPI mode 0
       devcfg1.queue_size=1700;                          //We want to be able to queue 7 transactions at a time
                     

   spi_bus_config_t buscfg1={
         .miso_io_num=PIN_NUM_MISO,
         .mosi_io_num=PIN_NUM_MOSI,
         .sclk_io_num=PIN_NUM_CLK,
         .quadwp_io_num=-1,
         .quadhd_io_num=-1
     };


   //Initialize the SPI bus
     ret=spi_bus_initialize(HSPI_HOST,&buscfg1,1);
     assert(ret==ESP_OK);
     ret=spi_bus_add_device(HSPI_HOST, &devcfg1, &spi_lcd);


   lcd_pin_init();

   gpio_set_direction(PIN_NUM_MISO, GPIO_MODE_INPUT);
   gpio_set_direction(PIN_NUM_MOSI, GPIO_MODE_OUTPUT);
   gpio_set_direction(PIN_NUM_CLK, GPIO_MODE_OUTPUT);
  
        CLEAR_PERI_REG_MASK(SPI_USER_REG(SPI_NUM), SPI_USR_COMMAND);
        SET_PERI_REG_BITS(SPI_USER2_REG(SPI_NUM), SPI_USR_COMMAND_BITLEN, 0, SPI_USR_COMMAND_BITLEN_S);
        CLEAR_PERI_REG_MASK(SPI_USER_REG(SPI_NUM), SPI_USR_ADDR);
        SET_PERI_REG_BITS(SPI_USER1_REG(SPI_NUM), SPI_USR_ADDR_BITLEN, 0, SPI_USR_ADDR_BITLEN_S);



}


 void  send2 (char data1, char cd)
 {

  uint32_t data=0;

   WRITE_PERI_REG(SPI_CLOCK_REG(SPI_NUM), (1 << SPI_CLKCNT_N_S) | (1 << SPI_CLKCNT_L_S));//40MHz

   SET_PERI_REG_MASK(SPI_USER_REG(SPI_NUM), SPI_USR_MOSI);
   SET_PERI_REG_BITS(SPI_MOSI_DLEN_REG(SPI_NUM), SPI_USR_MOSI_DBITLEN, 0x7, SPI_USR_MOSI_DBITLEN_S);

 //enable ADDRess function in SPI module
    SET_PERI_REG_MASK(SPI_USER_REG(SPI_NUM), SPI_USR_ADDR);
    SET_PERI_REG_BITS(SPI_USER1_REG(SPI_NUM), SPI_USR_ADDR_BITLEN,0, SPI_USR_ADDR_BITLEN_S);


   gpio_set_level(GPIO_CE, 0);//CS=0;

   while (READ_PERI_REG(SPI_CMD_REG(SPI_NUM))&SPI_USR)
   {
   }
     WRITE_PERI_REG(SPI_ADDR_REG(SPI_NUM), (uint32_t)cd<<31); // //align address data to high bits
     WRITE_PERI_REG((SPI_W0_REG(SPI_NUM)), (uint32_t)data1);
     SET_PERI_REG_MASK(SPI_CMD_REG(SPI_NUM), SPI_USR);

     while (READ_PERI_REG(SPI_CMD_REG(SPI_NUM))&SPI_USR)
     {
     }
    gpio_set_level(GPIO_CE, 1);//CS=1;

}

// توابع سازگار با کد قدیمی
void _D(uint8_t data) {
  //  send_9bit(data, true);  // Data
     send2(data, true);  // Data
}

void _C(uint8_t data) {
   // send_9bit(data, false); // Command
    send2(data, false);  // Data
}




void   InitLcd(uint16_t mod)
{

 ESP_LOGI(TAG, "Initializing LCD, mode: %d", mod);
    
    // راه‌اندازی SPI
    // esp_err_t ret = spi_master_init();
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to initialize SPI");
    //     return;
    // }

    spi_master_init1();

    // Soft Reset
    _C(0x01); // SWRESET
    Delay_ms(100);
    
    // Sleep Out
    _C(0x11); // SLEEPOUT
    Delay_ms(120);
    
    // Display Inversion On
    _C(0x21); // INVON
    Delay_ms(10);

  if(mod==12){
   //   send(RGBSET8,cmd);   // setup 8-bit color lookup table  [RRRGGGBB]
  _C(0x2d);
 //RED
    _D(0);
    _D(2  );
   _D(4  );
   _D(6  );
   _D(8  );
   _D(10  );
   _D(12  );
   _D(15  );
    // GREEN
   _D(0  );
   _D(2  );
   _D(4  );
   _D(6  );
   _D(8  );
   _D(10  );
   _D(12  );
   _D(15  );
    //BLUE
   _D(0  );
   _D(4  );
   _D(9  );
   _D(15  );

   _D(NOP  );  // nop

      //cs1();
   _D(NOP  );  // nop

      //cs1();
    _C(COLMOD);
 _D(0x03  ); // 0x03 = 12 bits-per-pixel
    //cs1();


  }

  if(mod==8){
  //_D(0x00  );
  _C(COLMOD);
  _D(0x2);
    }


    if(mod==16){
    _C(COLMOD);
  _D(0x05  );//0x05 = 16 bits-per-pixel


  }

//_C(0x2a); _D(0x00);  _D(0x00);  _D(0x00);  _D(0x83);
//_C(0x2b); _D(0x00);  _D(0x00);  _D(0x00);  _D(0xa1);

// ممکن است نیاز به تنظیم مجدد ستون و سطر داشته باشید
_C(0x2a); 
_D(0x00);  
_D(0x00);  
_D(0x00);  
_D(0x83); // عرض نمایشگر

_C(0x2b); 
_D(0x00);  
_D(0x00);  
_D(0x00);  
_D(0xa1); // ارتفاع نمایشگر

// Memory access controler (command 0x36)
   _C(MADCTL);
 // _D(0x00); // معمولی
// _D(0xC0); //180 عمودی
// _D(0xA0); // -90 درجه
// _D(0x60); // 270 درجه
    _D( 0x60);
     //cs1();
 // Write contrast (command 0x25)
  _C (SETCON);
  _D(0x35); // contrast 0x30
    //cs1();
  Delay_ms(20);
// Display On (command 0x29)
  _C(DISPON);
    //cs1();



// gammaAdjustmentST7735();

 //system_update_cpu_freq(160);
//spi_clock(HSPI, 1, 2); // spi clock hi

}



// تابع پاک کردن صفحه

void LCDClearScreen(uint16_t color) {
    long i;  // loop counter

    uint8_t l_color, h_color;

    // محدوده‌ها رو swap کنید برای landscape (ستون=0-161, ردیف=0-131)
    _C(0x2a); _D(0x00); _D(0x00); _D(0x00); _D(0xA1);  // Column: 0-161 (ارتفاع)
    _C(0x2b); _D(0x00); _D(0x00); _D(0x00); _D(0x83);  // Row: 0-131 (عرض)

    _C(RAMWR);  // Memory Write 0x2C

    color = color ^ 0xFFFF;  // invert? (اگر لازم، تست کنید بدون ^)
    l_color = color & 0x00FF;
    h_color = color >> 8;

    // ets_intr_lock(15);  // interrupts off (اگر لازم)
    // ets_wdt_disable();

    // لوپ تصحیح‌شده: 132 * 162 = 21384
    for (i = 0; i < (132 * 162); i++) {
        _D(h_color);
        _D(l_color);
    }

    // ets_wdt_enable();
    // ets_intr_unlock(15);
}



// توابع LVGL (در صورت نیاز)
static void set_addr_window(uint16_t x1, uint16_t x2, uint16_t y1, uint16_t y2) {
    _C(0x2A); // CASET
    _D((x1 >> 8) & 0xFF);
    _D(x1 & 0xFF);
    _D((x2 >> 8) & 0xFF);
    _D(x2 & 0xFF);

    _C(0x2B); // RASET
    _D((y1 >> 8) & 0xFF);
    _D(y1 & 0xFF);
    _D((y2 >> 8) & 0xFF);
    _D(y2 & 0xFF);

    _C(0x2C); // RAMWR
}


void lcd101_flush_horizontal(lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * color_map)
{
    uint16_t temp;
    uint32_t size;
    lv_color16_t* p16 = (lv_color16_t*) color_map;

    int32_t width = lv_area_get_width(area);
    int32_t height = lv_area_get_height(area);

    // همان set window معمولی (همون که برای عمودی/افقی استفاده می‌کنی)
    set_addr_window(area->x1, area->x2, area->y2, area->y2);

    size = (uint32_t)width * (uint32_t)height;

    // ارسال معکوسِ کامل پیکسل‌ها => چرخش 180 درجه
    for (int32_t i = (int32_t)size - 1; i >= 0; i--) {
        temp = p16[i].full ^ 0xFFFF;    // اگر در بقیه جاها هم ^0xFFFF استفاده می‌کنی، نگه دار
      
        _D((temp >> 8));     // high byte
          _D((temp & 0xFF));   // low byte
    }

    
}

 void lcd101_flush(lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * color_map)
 {
     uint16_t temp;
     uint8_t data[4];
     int32_t x;
     int32_t y;
     int i;
     uint32_t size ,cont=0;


     set_addr_window(area->x1,area->x2,area->y1,area->y2);

      size = lv_area_get_width(area) * lv_area_get_height(area);
      lv_color16_t* p16 = (lv_color16_t*) color_map;
        
      for (i=0; i<size; i++)
      {
          temp=p16[cont].full^0xffff;
           _D((temp >> 8 ) );   // high byte
          _D((temp & 0xff ) );  // low byte
                 cont=cont+1;

      }
      
 }


 

