/*
 * freq_and_filters.c
 *
 *  Created on: 2010-06-13
 *      Author: Loftur Jonasson, TF3LJ
 */

#include <stdint.h>
#include <stdio.h>

#include "freq_and_filters.h"
#include "Mobo_config.h"
#include "DG8SAQ_cmd.h"
#include "rotary_encoder.h"
#include "Si570.h"
#include "PCF8574.h"

#if LCD_DISPLAY            						// Multi-line LCD display
#include "taskLCD.h"
#endif

char frq_lcd[13];								// Pass frequency information to LCD
char flt_lcd[5];								// LCD Print formatting for filters

/*! \brief Set Band Pass and Low Pass filters
 *
 * \retval None or RX frequency band, depending on #define CALC_BAND_MUL_ADD
 */
#if CALC_BAND_MUL_ADD							// Band dependent Frequency Subtract and Multiply
uint8_t SetFilter(uint32_t freq)
#else
void SetFilter(uint32_t freq)
#endif
{
	uint8_t		selectedFilters[2]={0,0};		// Contains info on which filters are selected, for LCD print

	#if TX_FILTERS
	sint16_t band_sel;							// TX Filter selector, used with scrambled filters feature
	#endif

	#if CALC_BAND_MUL_ADD						// Band dependent Frequency Subtract and Multiply
	uint8_t freqBand=0, i;
	#else
	uint8_t i;
	#endif

	#if SCRAMBLED_FILTERS						// Enable a non contiguous order of filters
	uint8_t data;
	#endif

	sint32_t Freq;

	Freq.dw = freq;								// Freq.w1 is 11.5bits

	//-------------------------------------------
	// Set RX Band Pass filters
	//-------------------------------------------
	if(i2c.pcfmobo)
	{
		for (i = 0; i < 7; i++)
		{
			if (Freq.w0 < cdata.FilterCrossOver[i]) break;
		}
		#if SCRAMBLED_FILTERS						// Enable a non contiguous order of filters
		data = cdata.FilterNumber[i] & 0x07;		// We only want 3 bits
		pcf8574_mobo_data_out &=  0xf8;				// clear and leave upper 5 bits untouched
		pcf8574_mobo_set(cdata.PCF_I2C_Mobo_addr,data);	// Combine the two and write out
		selectedFilters[0] = cdata.FilterNumber[i];	// Used for LCD Print indication
		#else
		//i = i & 0x07;								// We only want 3 bits
		pcf8574_mobo_data_out &=  0xf8;				// clear and leave upper 5 bits untouched
		pcf8574_mobo_set(cdata.PCF_I2C_Mobo_addr,i);// Combine the two and write out
		selectedFilters[0] = i;						// Used for LCD Print indication
		#endif

		#if CALC_BAND_MUL_ADD						// Band dependent Frequency Subtract and Multiply
		freqBand = i;								// Band info used for Freq Subtract/Multiply feature
		#endif

	}
	else selectedFilters[0] = 0x0f;					// Error Indication, no PCF to write to

	#if TX_FILTERS
	//-------------------------------------------
	// Set TX Low Pass filters
	//-------------------------------------------
	for (i = 0; i < TXF-1; i++)
	{
		if (Freq.w0 < cdata.TXFilterCrossOver[i]) break;
	}

	#if	PCF_LPF									// External Port Expander Control of Low Pass filters
	// If we write to I2C without the device being present, then later I2C writes produce unexpected results
	if(i2c.pcflpf1)
	{
		#if SCRAMBLED_FILTERS						// Enable a non contiguous order of filters
		band_sel.w = 1<<cdata.TXFilterNumber[i];	// Set bit in a 16 bit register
		pcf8574_out_byte(cdata.PCF_I2C_lpf1_addr, band_sel.b1);
		#if	PCF_16LPF								// External Port Expander Control of 16 Low Pass filters
		// If we write without the device being present, later I2C writes produce unexpected results
		if(i2c.pcflpf2)
			pcf8574_out_byte(cdata.PCF_I2C_lpf2_addr, band_sel.b0);
		#endif
		selectedFilters[1] = cdata.TXFilterNumber[i];// Used for LCD Print indication
		#else
		band_sel.w = 1<<i;							// Set bit in a 16 bit register
		pcf8574_out_byte(cdata.PCF_I2C_lpf1_addr, band_sel.b1);
		#if	PCF_16LPF								// External Port Expander Control of 16 Low Pass filters
		// If we write without the device being present, later I2C writes produce unexpected results
		if(i2c.pcflpf2)
			pcf8574_out_byte(cdata.PCF_I2C_lpf2_addr, band_sel.b0);
		#endif
		selectedFilters[1] = i;						// Used for LCD Print indication
		#endif// SCRAMBLED_FILTERS
	}
	else selectedFilters[1] = 0x0f;					// Error indication
	#endif//PCF_LPF

	#if PCF_FILTER_IO							// 8x BCD control for LPF switching, switches P1 pins 4-6
	if(i2c.pcfmobo)
	{
		uint8_t	j;
		#if SCRAMBLED_FILTERS						// Enable a non contiguous order of filters
		band_sel.b1 = cdata.TXFilterNumber[i] & 0x07;// Set and Enforce bounds for a 3 bit value
		j = band_sel.b1<<3;							// leftshift x 3 for bits 3 - 5
		pcf8574_mobo_data_out &= 0b11000111;		// Clear out old data before adding new
		pcf8574_mobo_set(cdata.PCF_I2C_Mobo_addr,j);// Combine the two and write out
		selectedFilters[1] = cdata.TXFilterNumber[i];// Used for LCD Print indication
		#else
		band_sel.b1 = i & 0x07;						// Set and Enforce bounds for a 3 bit value
		j = band_sel.b1<<3;							// leftshift x 3 for bits 3 - 5
		pcf8574_mobo_data_out &= 0b11000111;		// Clear out old data before adding new
		pcf8574_mobo_set(cdata.PCF_I2C_Mobo_addr,j);// Combine the two and write out
		selectedFilters[1] = i;						// Used for LCD Print indication
		#endif
	}
	#endif

	#if M0RZF_FILTER_IO							// M0RZF 20W amplifier LPF switching, switches P1 pins 4-6
	if(i2c.pcfmobo)
	{
		uint8_t	j;									// filter value
		if      (i == 0) j = 0b00001000;			// First filter, as defined in data struct in AVR-Mobo.c
		else if (i == 1) j = 0b00010000;			// Second filter
		else if (i == 2) j = 0b00100000; 			// Third filter
		else             j = 0b00000000; 			// Default filter
		pcf8574_mobo_data_out &= 0b11000111;		// Clear out old data before adding new
		pcf8574_mobo_set(cdata.PCF_I2C_Mobo_addr,j);// Combine the two and write out
		selectedFilters[1] = i;						// Used for LCD Print indication
	}
	#endif
	#endif


	#if LCD_DISPLAY            	// Multi-line LCD display
	#if FRQ_IN_FIRST_LINE		// Normal Frequency display in first line of LCD. Can be disabled for Debug
	if (!MENU_mode)
   	{
   		// Display Selected Filters
   		sprintf(flt_lcd,"F%x-%x", selectedFilters[0],selectedFilters[1]);
   		xSemaphoreTake( mutexQueLCD, portMAX_DELAY );
   		lcd_q_goto(0,16);
   		lcd_q_print(flt_lcd);
   		xSemaphoreGive( mutexQueLCD );
   	}
	#endif
	#endif

   	#if CALC_BAND_MUL_ADD		// Band dependent Frequency Subtract and Multiply
	return freqBand;
	#endif
}

/*! \brief Si570 Set frequency (as a 32bit value) and filters,
 *
 * \retval TWI status
 */
uint8_t new_freq_and_filters(uint32_t freq)		// frequency [MHz] * 2^21
{
	uint8_t	status=0;			// Is the Si570 On Line?
	static uint8_t band;		// which BPF frequency band? (used with CALC_BAND_MUL_ADD)
	double set_frequency;		// Frequency in double precision floating point

	#if LCD_DISPLAY				// Multi-line LCD display
	double freq_display;		// Frequency to be displayed
	#endif

	// Translate frequency to a double precision float
	set_frequency = (double)freq/_2(21);

	// Enforce some sane frequency boundaries (1 - 100 MHz)
	if ((set_frequency < 4.0) || (set_frequency >= 400.0)) return 0;

	cdata.Freq[0] = freq;		// Some Command calls to this func do not update si570.Freq[0]

	//-------------------------------------------
	// Display Running Frequency
	//-------------------------------------------
	#if LCD_DISPLAY            	// Multi-line LCD display
	#if FRQ_IN_FIRST_LINE		// Normal Frequency display in first line of LCD. Can be disabled for Debug
	if (!MENU_mode)
   	{
   		freq_display = set_frequency/4;
   		// Add PSDR-IQ offset if in RX
   		if (!TX_flag) freq_display += cdata.LCD_RX_Offset/1000.0;
   		sprintf(frq_lcd,"%2.06fMHz", freq_display);
   		xSemaphoreTake( mutexQueLCD, portMAX_DELAY );
   		lcd_q_goto(0,3);
   		if(freq_display<=10) lcd_q_print(" ");	// workaround...%2 print format doesn't work
   		lcd_q_print(frq_lcd);
   		xSemaphoreGive( mutexQueLCD );
   	}
	#endif
	#endif

	#if BPF_LPF_Module			// Band Pass and Low Pass filter switcing
	#if !FRQ_CGH_DURING_TX		// Do not allow Si570 frequency change and corresponding filter change during TX
	if (TX_state)		 		// Oops, we are transmitting... return without changing frequency
		return TWI_INVALID_ARGUMENT;
	#endif

	#if !FLTR_CGH_DURING_TX		// Do not allow Filter changes when frequency is changed during TX
	if (!TX_state)				// Only change filters when not transmitting
	#endif
	#endif

	#if CALC_FREQ_MUL_ADD		// Frequency Subtract and Multiply Routines (for smart VFO)
								// Modify Si570 frequency according to Mul/Sub values
	set_frequency = (set_frequency - (double)cdata.FreqSub/_2(21))*(double)cdata.FreqMul/_2(21);

	#elif CALC_BAND_MUL_ADD		// Band dependent Frequency Subtract and Multiply
								// Modify Si570 frequency according to Mul/Sub values
	set_frequency = (set_frequency - (double)cdata.BandSub[band]/_2(21))*(double)cdata.BandMul[band]/_2(21);
	#endif

	status = SetFrequency(set_frequency);

	#if BPF_LPF_Module			// Band Pass and Low Pass filter switcing
	#if CALC_BAND_MUL_ADD		// Band dependent Frequency Subtract and Multiply
	band = SetFilter(freq);	// Select Band Pass Filter, according to the frequency selected
	#else
	SetFilter(freq);			// Select Band Pass Filter, according to the frequency selected
	#endif
	#endif

	return status;
}

/*! \brief Set Si570 Frequency + Manage BPF and LPF
 *
 * \retval none.
 */
void freq_and_filter_control(void)
{
	uint32_t	frq_from_encoder;

	if (i2c.si570)
    {
    	// Check for a frequency change request from USB or Encoder
		// USB always takes precedence
   		if(FRQ_fromusb == TRUE)
   		{
   			new_freq_and_filters(freq_from_usb);// Write usb frequency to Si570
   			FRQ_fromusb = FALSE;				// Clear both input flags
   			FRQ_fromenc = FALSE;				// USB takes precedence
   		}
   		// This is ignored while in Menu Mode, then Menu uses the Encoder.
   		if(FRQ_fromenc == TRUE)
   		{
   			if (!MENU_mode)
			{
				// Add the accumulated but yet unenacted frequency delta from the
				// encoder to the current Si570 frequency
				frq_from_encoder = cdata.Freq[0] + freq_delta_from_enc;
				freq_delta_from_enc = 0;			// Zero the accumulator
				FRQ_fromenc = FALSE;				// Clear input flag

				new_freq_and_filters(frq_from_encoder);// Write new freq to Si570
			}
			else freq_delta_from_enc = 0;			// Zero any changes while Menu Control
   		}
    }
	#if LCD_DISPLAY      						    // Multi-line LCD display
	#if FRQ_IN_FIRST_LINE							// Normal Frequency display in first line of LCD. Can be disabled for Debug
	else
    {
       	if (!MENU_mode)
       	{
        	xSemaphoreTake( mutexQueLCD, portMAX_DELAY );
        	lcd_q_goto(0,3);
        	lcd_q_print("No Si570 OSC");
        	xSemaphoreGive( mutexQueLCD );
       	}
    }
	#endif
	#endif
}

