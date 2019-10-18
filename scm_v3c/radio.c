#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "Memory_Map.h"
#include "scm3c_hardware_interface.h"
#include "scm3_hardware_interface.h"
#include "radio.h"
#include "rftimer.h"
#include "bucket_o_functions.h"

extern char send_packet[127];
extern char recv_packet[130];

unsigned int chips[100];
unsigned int chip_index = 0;
int raw_chips;
int jj;
unsigned int acfg3_val;

extern unsigned int LC_code;
extern unsigned int IF_clk_target;
extern unsigned int IF_coarse;
extern unsigned int IF_fine;
extern unsigned int cal_iteration;
extern unsigned int ASC[38];

unsigned int num_packets_received;
unsigned int num_crc_errors;
unsigned int wrong_lengths;
unsigned int LQI_chip_errors;
unsigned int num_valid_packets_received;
unsigned int IF_estimate;

unsigned int current_thresh = 0;
extern unsigned int run_test_flag;
extern unsigned int num_packets_to_test;
signed short cdr_tau_value;

extern unsigned int HF_CLOCK_fine;
extern unsigned int HF_CLOCK_coarse;
extern unsigned int RC2M_superfine;
extern unsigned int RC2M_fine;
extern unsigned int RC2M_coarse;

extern unsigned int RX_channel_codes[16];
extern unsigned int TX_channel_codes[16];

signed int SFD_timestamp = 0;
signed int SFD_timestamp_n_1 = 0;
signed int timing_correction = 0;
extern unsigned short doing_initial_packet_search;

// Timer parameters 
extern unsigned int packet_interval;
extern unsigned int radio_startup_time;
extern unsigned int expected_RX_arrival;
extern unsigned int ack_turnaround_time;
extern unsigned int guard_time;
extern unsigned short current_RF_channel;

extern unsigned short do_debug_print;

// These coefficients are used for filtering frequency feedback information
// These are no necessarily the ideal values to use; situationally dependent
unsigned char FIR_coeff[11] = {4,16,37,64,87,96,87,64,37,16,4};
unsigned int IF_estimate_history[11] = {500,500,500,500,500,500,500,500,500,500};
signed short cdr_tau_history[11] = {0};

// How many packets must be received before adjusting RX clock rates
// Should be at least as long as the FIR filters
unsigned short frequency_update_rate = 15; 
unsigned short frequency_update_cooldown_timer;

//=========================== definition ======================================

#define MAXLENGTH_TRX_BUFFER    128     // 1B length, 125B data, 2B CRC

//===== default crc check result and rssi value

#define DEFAULT_CRC_CHECK        01     // this is an arbitrary value for now
#define DEFAULT_RSSI            -50     // this is an arbitrary value for now
#define DEFAULT_FREQ             11     // use the channel 11 for now

//===== for calibration

#define  IF_FREQ_UPDATE_TIMEOUT  10
#define  LO_FREQ_UPDATE_TIMEOUT  10
#define  FILTER_WINDOWS_LEN      11
#define  FIR_COEFF_SCALE         512 // determined by FIR_coeff

//===== for recognizing panid

#define  LEN_PKT_INDEX           0x00
#define  PANID_LBYTE_PKT_INDEX   0x04
#define  PANID_HBYTE_PKT_INDEX   0x05
#define  DEFAULT_PANID           0xcafe

//=========================== variables =======================================

typedef struct {
    radio_capture_cbt   startFrame_tx_cb;
    radio_capture_cbt   endFrame_tx_cb;
    radio_capture_cbt   startFrame_rx_cb;
    radio_capture_cbt   endFrame_rx_cb;
    
    uint8_t             radio_tx_buffer[MAXLENGTH_TRX_BUFFER] __attribute__ \
                                                            ((aligned (4)));
    uint8_t             radio_rx_buffer[MAXLENGTH_TRX_BUFFER] __attribute__ \
                                                            ((aligned (4)));
    uint8_t             current_frequency;
    bool                crc_ok;
} radio_vars_t;

radio_vars_t radio_vars;

//=========================== prototypes ======================================

void setFrequencyTX(uint8_t channel);
void setFrequencyRX(uint8_t channel);

//=========================== public ==========================================


void radio_init(void) {

    // clear variables
    memset(&radio_vars,0,sizeof(radio_vars_t));

    // Enable radio interrupts in NVIC
    ISER = 0x40;
    
    // enable sfd done and send done interruptions of tranmission
    // enable sfd done and receiving done interruptions of reception
    RFCONTROLLER_REG__INT_CONFIG    = TX_LOAD_DONE_INT_EN           |   \
                                      TX_SFD_DONE_INT_EN            |   \
                                      TX_SEND_DONE_INT_EN           |   \
                                      RX_SFD_DONE_INT_EN            |   \
                                      RX_DONE_INT_EN;

    // Enable all errors
//    RFCONTROLLER_REG__ERROR_CONFIG  = TX_OVERFLOW_ERROR_EN          |   \
//                                      TX_CUTOFF_ERROR_EN            |   \
//                                      RX_OVERFLOW_ERROR_EN          |   \
//                                      RX_CRC_ERROR_EN               |   \
//                                      RX_CUTOFF_ERROR_EN;
                                      
    RFCONTROLLER_REG__ERROR_CONFIG  = RX_CRC_ERROR_EN;
}

void radio_setStartFrameTxCb(radio_capture_cbt cb) {
    radio_vars.startFrame_tx_cb    = cb;
}

void radio_setEndFrameTxCb(radio_capture_cbt cb) {
    radio_vars.endFrame_tx_cb      = cb;
}

void radio_setStartFrameRxCb(radio_capture_cbt cb) {
    radio_vars.startFrame_rx_cb    = cb;
}

void radio_setEndFrameRxCb(radio_capture_cbt cb) {
    radio_vars.endFrame_rx_cb      = cb;
}

void radio_reset(void) {
    // reset SCuM radio module
    RFCONTROLLER_REG__CONTROL = RF_RESET;
}

void radio_setFrequency(uint8_t frequency, radio_freq_t tx_or_rx) {
    
    radio_vars.current_frequency = DEFAULT_FREQ;
    
    switch(tx_or_rx){
    case 0x01:
        setFrequencyTX(radio_vars.current_frequency);
        break;
    case 0x02:
        setFrequencyRX(radio_vars.current_frequency);
        break;
    default:
        // shouldn't happen
        break;
    }
}

void radio_loadPacket(uint8_t* packet, uint16_t len){
    
    memcpy(&radio_vars.radio_tx_buffer[0],packet,len);

    // load packet in TXFIFO
    RFCONTROLLER_REG__TX_DATA_ADDR  = &(radio_vars.radio_tx_buffer[0]);
    RFCONTROLLER_REG__TX_PACK_LEN   = len;

    RFCONTROLLER_REG__CONTROL       = TX_LOAD;
}

// Turn on the radio for transmit
// This should be done at least ~50 us before txNow()
void radio_txEnable(){
    
    // Turn off polyphase and disable mixer
    ANALOG_CFG_REG__16 = 0x6;
    
    // Turn on LO, PA, and AUX LDOs
    ANALOG_CFG_REG__10 = 0x0028;
}

// Begin modulating the radio output for TX
// Note that you need some delay before txNow() to allow txLoad() to finish loading the packet
void radio_txNow(){
    
    RFCONTROLLER_REG__CONTROL = TX_SEND;
}

// Turn on the radio for receive
// This should be done at least ~50 us before rxNow()
void radio_rxEnable(){
    
    // Turn on LO, IF, and AUX LDOs via memory mapped register
    ANALOG_CFG_REG__10 = 0x0018;
    
    // Enable polyphase and mixers via memory-mapped I/O
    ANALOG_CFG_REG__16 = 0x1;
    
    // Where packet will be stored in memory
    DMA_REG__RF_RX_ADDR = &(radio_vars.radio_rx_buffer[0]);;
    
    // Reset radio FSM
    RFCONTROLLER_REG__CONTROL = RF_RESET;
}

// Radio will begin searching for start of packet
void radio_rxNow(){
    
    // Reset digital baseband
    ANALOG_CFG_REG__4 = 0x2000;
    ANALOG_CFG_REG__4 = 0x2800;

    // Start RX FSM
    RFCONTROLLER_REG__CONTROL = RX_START;
}

void radio_getReceivedFrame(uint8_t* pBufRead,
                            uint8_t* pLenRead,
                            uint8_t  maxBufLen,
                             int8_t* pRssi,
                            uint8_t* pLqi) {
   
    //===== rssi
    *pRssi          = DEFAULT_RSSI;
    
    //===== length
    *pLenRead       = radio_vars.radio_rx_buffer[0];
    
    //===== packet 
    if (*pLenRead<=maxBufLen) {
        memcpy(pBufRead,&(radio_vars.radio_rx_buffer[1]),*pLenRead);
    }
}

void radio_rfOff(){
    
    // Hold digital baseband in reset
    ANALOG_CFG_REG__4 = 0x2000;

    // Turn off LDOs
    ANALOG_CFG_REG__10 = 0x0000;
}

void radio_frequency_housekeeping(){
    
    signed int sum = 0;
    int jj;
    unsigned int IF_est_filtered;
    signed int chip_rate_error_ppm, chip_rate_error_ppm_filtered;
    unsigned short packet_len;
    signed int timing_correction;
    
    packet_len = radio_vars.radio_rx_buffer[0];
    
    // When updating LO and IF clock frequncies, must wait long enough for the changes to propagate before changing again
    // Need to receive as many packets as there are taps in the FIR filter
    frequency_update_cooldown_timer++;
    
    // FIR filter for cdr tau slope
    sum = 0;
    
    // A tau value of 0 indicates there is no rate mistmatch between the TX and RX chip clocks
    // The cdr_tau_value corresponds to the number of samples that were added or dropped by the CDR
    // Each sample point is 1/16MHz = 62.5ns
    // Need to estimate ppm error for each packet, then FIR those values to make tuning decisions
    // error_in_ppm = 1e6 * (#adjustments * 62.5ns) / (packet length (bytes) * 64 chips/byte * 500ns/chip)
    // Which can be simplified to (#adjustments * 15625) / (packet length * 8)
                
    chip_rate_error_ppm = (cdr_tau_value * 15625) / (packet_len * 8);
    
    // Shift old samples
    for (jj=9; jj>=0; jj--){
        cdr_tau_history[jj+1] = cdr_tau_history[jj];        
    }
    
    // New sample
    cdr_tau_history[0] = chip_rate_error_ppm;
    
    // Do FIR convolution
    for (jj=0; jj<=10; jj++){
        sum = sum + cdr_tau_history[jj] * FIR_coeff[jj];        
    }
    
    // Divide by 512 (sum of the coefficients) to scale output
    chip_rate_error_ppm_filtered = sum / 512;
    
    //printf("%d -- %d\r\n",cdr_tau_value,chip_rate_error_ppm_filtered);
    
    // The IF clock frequency steps are about 2000ppm, so make an adjustment only if the error is larger than 1000ppm
    // Must wait long enough between changes for FIR to settle (at least 10 packets)
    // Need to add some handling here in case the IF_fine code will rollover with this change (0 <= IF_fine <= 31)
    if(frequency_update_cooldown_timer == frequency_update_rate){
        if(chip_rate_error_ppm_filtered > 1000) {
            set_IF_clock_frequency(IF_coarse, IF_fine++, 0);
        }
        if(chip_rate_error_ppm_filtered < -1000) {
            set_IF_clock_frequency(IF_coarse, IF_fine--, 0);
        }
    }
    
    
    // FIR filter for IF estimate
    sum = 0;
                
    // The IF estimate reports how many zero crossings (both pos and neg) there were in a 100us period
    // The IF should on average be 2.5 MHz, which means the IF estimate will return ~500 when there is no IF error
    // Each tick is roughly 5 kHz of error
    
    // Only make adjustments when the chip error rate is <10% (this value was picked as an arbitrary choice)
    // While packets can be received at higher chip error rates, the average IF estimate tends to be less accurate
    // Estimated chip_error_rate = LQI_chip_errors/256 (assuming the packet length was at least 8 Bytes)
    if(LQI_chip_errors < 25){
    
        // Shift old samples
        for (jj=9; jj>=0; jj--){
            IF_estimate_history[jj+1] = IF_estimate_history[jj];        
        }
        
        // New sample
        IF_estimate_history[0] = IF_estimate;

        // Do FIR convolution
        for (jj=0; jj<=10; jj++){
            sum = sum + IF_estimate_history[jj] * FIR_coeff[jj];        
        }
        
        // Divide by 512 (sum of the coefficients) to scale output
        IF_est_filtered = sum / 512;
        
        //printf("%d - %d, %d\r\n",IF_estimate,IF_est_filtered,LQI_chip_errors);
        
        // The LO frequency steps are about ~80-100 kHz, so make an adjustment only if the error is larger than that
        // These hysteresis bounds (+/- X) have not been optimized
        // Must wait long enough between changes for FIR to settle (at least as many packets as there are taps in the FIR)
        // For now, assume that TX/RX should both be updated, even though the IF information is only from the RX code
        if(frequency_update_cooldown_timer == frequency_update_rate){
            if(IF_est_filtered > 520){
                RX_channel_codes[radio_vars.current_frequency - 11]++; 
                TX_channel_codes[radio_vars.current_frequency - 11]++; 
            }
            if(IF_est_filtered < 480){
                RX_channel_codes[radio_vars.current_frequency - 11]--; 
                TX_channel_codes[radio_vars.current_frequency - 11]--; 
            }
            
            //printf("--%d - %d\r\n",IF_estimate,IF_est_filtered);

            frequency_update_cooldown_timer = 0;
        }
    }
}

void radio_enable_interrupts(){
    
    // Enable radio interrupts in NVIC
    ISER = 0x40;
    
    // Enable all interrupts and pulses to radio timer
    //RFCONTROLLER_REG__INT_CONFIG = 0x3FF;   
        
    // Enable TX_SEND_DONE, RX_SFD_DONE, RX_DONE
    RFCONTROLLER_REG__INT_CONFIG = 0x1C;
    
    // Enable all errors
    //RFCONTROLLER_REG__ERROR_CONFIG = 0x1F;  
    
    // Enable only the RX CRC error
    RFCONTROLLER_REG__ERROR_CONFIG = 0x8;    //0x10; x10 is wrong? 
}

void radio_disable_interrupts(void){

    // Clear radio interrupts in NVIC
    ICER = 0x40;
}

bool radio_getCrcOk(void){
    return radio_vars.crc_ok;
}

//=========================== private =========================================

// SCM has separate setFrequency functions for RX and TX because of the way the
// radio is built. The LO needs to be set to a different frequency for TX vs RX
void setFrequencyRX(uint8_t channel){
    
    // Set LO code for RX channel
    LC_monotonic(RX_channel_codes[channel-11]);
}

void setFrequencyTX(uint8_t channel){
    
    // Set LO code for TX channel
    LC_monotonic(TX_channel_codes[channel-11]);
}

//=========================== intertupt =======================================

void radio_isr(void) {
    
    unsigned int interrupt = RFCONTROLLER_REG__INT;
    unsigned int error     = RFCONTROLLER_REG__ERROR;
    
    radio_vars.crc_ok   = true;
    if (error != 0) {
        
        printf("Radio ERROR\r\n");
        
        if (error & 0x00000001) {
            printf("TX OVERFLOW ERROR\r\n");
        }
        if (error & 0x00000002) {
            printf("TX CUTOFF ERROR\r\n");
        }
        if (error & 0x00000004) {
            printf("RX OVERFLOW ERROR\r\n");
        }
        
        if (error & 0x00000008) {
            printf("RX CRC ERROR\r\n");
            radio_vars.crc_ok   = false;
        }
        if (error & 0x00000010) {
            printf("RX CUTOFF ERROR\r\n");
        }
        
    }
    RFCONTROLLER_REG__ERROR_CLEAR = error;
    
    if (interrupt & 0x00000001) {
        printf("TX LOAD DONE\r\n");
    }
    
    if (interrupt & 0x00000002) {
        printf("TX SFD DONE\r\n");
        
        if (radio_vars.startFrame_tx_cb != 0) {
            radio_vars.startFrame_tx_cb(RFTIMER_REG__COUNTER);
        }
    }
    
    if (interrupt & 0x00000004){
        printf("TX SEND DONE\r\n");
        
        if (radio_vars.endFrame_tx_cb != 0) {
            radio_vars.endFrame_tx_cb(RFTIMER_REG__COUNTER);
        }
    }
    
    if (interrupt & 0x00000008){
        printf("RX SFD DONE\r\n");
        
        if (radio_vars.startFrame_rx_cb != 0) {
            radio_vars.startFrame_rx_cb(RFTIMER_REG__COUNTER);
        }
    }
    
    if (interrupt & 0x00000010) {
        
        printf("RX DONE\r\n");
        
        if (radio_vars.endFrame_rx_cb != 0) {
            radio_vars.endFrame_rx_cb(RFTIMER_REG__COUNTER);
        }
    }
    
    RFCONTROLLER_REG__INT_CLEAR = interrupt;
}


// This ISR goes off when the raw chip shift register interrupt goes high
// It reads the current 32 bits and then prints them out after N cycles
void rawchips_32_isr() {
    
    unsigned int jj;
    unsigned int rdata_lsb, rdata_msb;
    
    // Read 32bit val
    rdata_lsb = ANALOG_CFG_REG__17;
    rdata_msb = ANALOG_CFG_REG__18;
    chips[chip_index] = rdata_lsb + (rdata_msb << 16);    
        
    chip_index++;
    
    //printf("x1\r\n");
    
    // Clear the interrupt
    //ANALOG_CFG_REG__0 = 1;
    //ANALOG_CFG_REG__0 = 0;
    acfg3_val |= 0x20;
    ANALOG_CFG_REG__3 = acfg3_val;
    acfg3_val &= ~(0x20);
    ANALOG_CFG_REG__3 = acfg3_val;

    if(chip_index == 10){    
        for(jj=1;jj<10;jj++){
            printf("%X\r\n",chips[jj]);
        }

        ICER = 0x0100;
        ISER = 0x0200;
        chip_index = 0;
        
        // Wait for print to complete
        for(jj=0;jj<10000;jj++);
        
        // Execute soft reset
        *(unsigned int*)(0xE000ED0C) = 0x05FA0004;
    }
}

// With HCLK = 5MHz, data rate of 1.25MHz tested OK
// For faster data rate, will need to raise the HCLK frequency
// This ISR goes off when the input register matches the target value
void rawchips_startval_isr() {
    
    unsigned int rdata_lsb, rdata_msb;
    
    // Clear all interrupts
    acfg3_val |= 0x60;
    ANALOG_CFG_REG__3 = acfg3_val;
    acfg3_val &= ~(0x60);
    ANALOG_CFG_REG__3 = acfg3_val;
        
    // Enable the interrupt for the 32bit 
    ISER = 0x0200;
    ICER = 0x0100;
    ICPR = 0x0200;
    
    // Read 32bit val
    rdata_lsb = ANALOG_CFG_REG__17;
    rdata_msb = ANALOG_CFG_REG__18;
    chips[chip_index] = rdata_lsb + (rdata_msb << 16);
    chip_index++;

    //printf("x2\r\n");

}
