"""
Lydia Lee, 2019
This file contains the code to run various tests on the ADC. The functions in this file
are meant to communicate with other entities like the Teensy, SCM, and others. For post-
processing and data reading/writing/plotting, please see data_handling.py. This assumes
that you're working strictly through the Cortex and that outputs will be read from the
UART.
"""
import numpy as np
import scipy as sp
import serial
import visa
import time
import random
from data_handling import *
from cortex import *
from adc_fsm import *


def test_adc_spot(port="COM16", control_mode='uart', read_mode='uart', iterations=1,
		gpio_settings=dict()):
	"""
	Inputs:
		port: String. Name of the COM port that the UART or the reading Teensy
			is connected to.
		control_mode: String 'uart' or 'gpio'. Determines if you'll be triggering
			the FSM via UART or GPIO.
		read_mode: String 'uart' or 'gpio'. Determines if you're reading via GPIO.
		iterations: Integer. Number of times to take readings.
		gpio_settings: Dictionary with the following key:value pairings
			adc_settle_cycles: Integer [1,256]. Unclear what the control in the original 
				FSM is, but this will dictate the number of microseconds to wait 
				for the ADC to settle. Applicable only if the Teensy is used rather than
				UART.
			pga_bypass: Integer 0 or 1. 1 = bypass the PGA, otherwise use the PGA. Applicable
				only if the Teensy is used rather than UART.
			pga_settle_us: Integer. Number of microseconds to wait for the PGA output to
				to settle. Applicable only if the Teensy is used rather than
				UART.
	Outputs:
		Returns an ordered collection of ADC 
		output codes where the ith element corresponds to the ith reading.
		Note that this assumes that SCM has already been programmed.
	Raises:
		ValueError if the control mode is not 'uart' or 'gpio'.
		ValueError if the read mode is not 'uart' or 'gpio'.
	"""
	adc_outs = []

	ser = serial.Serial(
		port=port,
		baudrate=19200,
		parity=serial.PARITY_NONE,
		stopbits=serial.STOPBITS_ONE,
		bytesize=serial.EIGHTBITS,
		timeout=.5)

	if control_mode == 'gpio':
		initialize_gpio(teensy_ser)

	for i in range(iterations):
		if control_mode == 'uart':
			trigger_uart(ser)
		elif control_mode == 'gpio':
			adc_settle_cycles = gpio_settings['adc_settle_cycles']
			pga_bypass = gpio_settings['pga_bypass']
			pga_settle_us = gpio_settings['pga_settle_us']
			trigger_gpi(ser, adc_settle_cycles, pga_bypass, pga_settle_us)
		else:
			raise ValueError("Invalid control mode.")
		
		time.sleep(.5)

		if read_mode == 'uart':
			adc_out = read_uart(ser)
		elif read_mode == 'gpio':
			adc_out = read_gpo(ser)
		else:
			raise ValueError("Invalid read mode.")

		adc_outs.append(adc_out)
	
	ser.close()

	return adc_outs

def test_adc_psu(
		vin_vec, port="COM18", control_mode='uart', read_mode='uart',
		psu_name='USB0::0x0957::0x2C07::MY57801384::0::INSTR',
		iterations=1, gpio_settings=dict()):
	"""
	Inputs:
		vin_vec: 1D collection of floats. Input voltages in volts 
			to feed to the ADC.
		port: String. Name of the COM port that the UART or the reading Teensy
			is connected to.
		control_mode: String 'uart' or 'gpio'. Determines if you'll be triggering
			the FSM via UART or GPIO.
		read_mode: String 'uart' or 'gpio'. Determines if you're reading via GPIO.
		psu_name: String. Name to use in the connection for the 
			waveform generator.
		iterations: Integer. Number of times to take a reading for
			a single input voltage.
		gpio_settings: Dictionary with the following key:value pairings
			adc_settle_cycles: Integer [1,256]. Unclear what the control in the original 
				FSM is, but this will dictate the number of microseconds to wait 
				for the ADC to settle. Applicable only if the Teensy is used rather than
				UART.
			pga_bypass: Integer 0 or 1. 1 = bypass the PGA, otherwise use the PGA. Applicable
				only if the Teensy is used rather than UART.
			pga_settle_us: Integer. Number of microseconds to wait for the PGA output to
				to settle. Applicable only if the Teensy is used rather than
				UART.
	Outputs:
		Returns a dictionary adc_outs where adc_outs[vin][i] will give the 
		ADC code associated with the i'th iteration when the input
		voltage is 'vin'.

		Uses a serial as well as an arbitrary waveform generator to
		sweep the input voltage to the ADC. Bypasses the PGA. Does 
		NOT program scan.
	Raises:
		ValueError if the control mode is not 'uart' or 'gpio'.
		ValueError if the read mode is not 'uart' or 'gpio'.
	"""
	# Opening up the UART connection
	ser = serial.Serial(
		port=port,
		baudrate=19200,
		parity=serial.PARITY_NONE,
		stopbits=serial.STOPBITS_ONE,
		bytesize=serial.EIGHTBITS,
		timeout=.5)


	# Connecting to the arbitrary waveform generator
	rm = visa.ResourceManager()
	psu = rm.open_resource(psu_name)

	# Sanity checking that it's the correct device
	psu.query("*IDN?")
	
	# Managing settings for the waveform generator appropriately
	psu.write("OUTPUT2:LOAD INF")
	psu.write("SOURCE2:FUNCTION DC")

	# Setting the waveform generator to 0 initially to 
	# avoid breaking things
	psu.write("SOURCE2:VOLTAGE:OFFSET 0")
	psu.write("OUTPUT2 ON")

	# Sweeping vin and getting the ADC output code

	if control_mode == 'gpio':
		initialize_gpio(teensy_ser)

	adc_outs = dict()
	for vin in vin_vec:
		adc_outs[vin] = []
		psu.write("SOURCE2:VOLTAGE:OFFSET {}".format(vin))
		for i in range(iterations):
			if control_mode == 'uart':
				# trigger_uart(ser)
				trigger_gpio_loopback(ser)
			elif control_mode == 'gpio':
				adc_settle_cycles = gpio_settings['adc_settle_cycles']
				pga_bypass = gpio_settings['pga_bypass']
				pga_settle_us = gpio_settings['pga_settle_us']
				trigger_gpi(ser, adc_settle_cycles, pga_bypass, pga_settle_us)
			else:
				raise ValueError("Invalid control mode.")
			
			time.sleep(.5)

			if read_mode == 'uart':
				adc_out = read_uart(ser)
			elif read_mode == 'gpio':
				adc_out = read_gpo(ser)
			else:
				raise ValueError("Invalid read mode.")
			
			adc_outs[vin].append(adc_out)
			print("Vin={}V/Iteration {} -- {}".format(vin, i, adc_outs[vin][i]))

	# Due diligence for closing things out
	ser.close()
	psu.write("SOURCE2:VOLTAGE:OFFSET 0")
	psu.write("OUTPUT2 OFF")
	psu.close()
	
	return adc_outs

def test_adc_spot_loopback(port="COM16", iterations=1):
	"""
	Inputs:
		port: String. Name of the COM port that the UART or the reading Teensy
			is connected to.
	Outputs:
		Returns an ordered collection of ADC output codes where the 
		ith element corresponds to the ith reading. Note that this assumes 
		that SCM has already been programmed.
	"""
	adc_outs = []

	ser = serial.Serial(
		port=port,
		baudrate=19200,
		parity=serial.PARITY_NONE,
		stopbits=serial.STOPBITS_ONE,
		bytesize=serial.EIGHTBITS,
		timeout=.5)

	trigger_gpio_loopback(ser)
	for i in range(iterations):
		print(ser.readline())
		print(ser.readline())
		print(ser.readline())
		print(ser.readline())
		# adc_out = read_uart(ser)
		# print(adc_out)
		# adc_outs.append(adc_out)

	return adc_outs


def test_adc_dnl(vin_min, vin_max, num_bits, 
		port="COM18", control_mode='uart', read_mode='uart',
		psu_name='USB0::0x0957::0x2C07::MY57801384::0::INSTR',
		iterations=1, gpio_settings=dict()):
	"""
	Inputs:
		vin_min/max: Float. The min and max voltages to feed to the
			ADC.
		num_bits: Integer. The bit resolution of the ADC.
		port: String. Name of the COM port that the UART or the reading Teensy
			is connected to.
		control_mode: String 'uart' or 'gpio'. Determines if you'll be triggering
			the FSM via UART or GPIO.
		read_mode: String 'uart' or 'gpio'. Determines if you're reading via GPIO.
		psu_name: String. Name to use in the connection for the 
			waveform generator.
		iterations: Integer. Number of times to take a reading for
			a single input voltage.
		gpio_settings: Dictionary with the following key:value pairings
			adc_settle_cycles: Integer [1,256]. Unclear what the control in the original 
				FSM is, but this will dictate the number of microseconds to wait 
				for the ADC to settle. Applicable only if the Teensy is used rather than
				UART.
			pga_bypass: Integer 0 or 1. 1 = bypass the PGA, otherwise use the PGA. Applicable
				only if the Teensy is used rather than UART.
			pga_settle_us: Integer. Number of microseconds to wait for the PGA output to
				to settle. Applicable only if the Teensy is used rather than
				UART.
	Outputs:
		Returns a collection of DNLs for each nominal LSB. The length
		should be 2**(num_bits)-1. Note that this assumes that SCM has 
		already been programmed.
	"""
	# Constructing the vector of input voltages to give the ADC
	vlsb_ideal = (vin_max-vin_min)/2**num_bits
	vin_vec = np.arange(vin_min, vin_max, vlsb_ideal/10)

	# Running the test
	adc_outs = test_adc_psu(vin_vec, port, control_mode, read_mode, \
							psu_name, iterations, gpio_settings)
	
	return calc_adc_dnl(adc_outs, vlsb_ideal)


def test_adc_inl_straightline(vin_min, vin_max, num_bits, 
		port="COM18", control_mode='uart', read_mode='uart',
		psu_name='USB0::0x0957::0x2C07::MY57801384::0::INSTR',
		iterations=1, gpio_settings=dict()):
	"""
	Inputs:
		vin_min/max: Float. The min and max voltages to feed to the
			ADC.
		num_bits: Integer. The bit resolution of the ADC.
		port: String. Name of the COM port that the UART or the reading Teensy
			is connected to.
		control_mode: String 'uart' or 'gpio'. Determines if you'll be triggering
			the FSM via UART or GPIO.
		read_mode: String 'uart' or 'gpio'. Determines if you're reading via GPIO.
		psu_name: String. Name to use in the connection for the 
			waveform generator.
		iterations: Integer. Number of times to take a reading for
			a single input voltage.
		gpio_settings: Dictionary with the following key:value pairings
			adc_settle_cycles: Integer [1,256]. Unclear what the control in the original 
				FSM is, but this will dictate the number of microseconds to wait 
				for the ADC to settle. Applicable only if the Teensy is used rather than
				UART.
			pga_bypass: Integer 0 or 1. 1 = bypass the PGA, otherwise use the PGA. Applicable
				only if the Teensy is used rather than UART.
			pga_settle_us: Integer. Number of microseconds to wait for the PGA output to
				to settle. Applicable only if the Teensy is used rather than
				UART.
	Outputs:
		Returns the slope, intercept of the linear regression
		of the ADC code vs. vin. Note that this assumes that SCM 
		has already been programmed.
	"""
	# Constructing the vector of input voltages to give the ADC
	vlsb_ideal = (vin_max-vin_min)/2**num_bits
	vin_vec = np.arange(vin_min, vin_max, vlsb_ideal/4)

	# Running the test
	adc_outs = test_adc_psu(vin_vec, port, control_mode, read_mode, \
							psu_name, iterations, gpio_settings)

	return calc_adc_inl_straightline(adc_outs, vlsb_ideal)


def test_adc_inl_endpoint(vin_min, vin_max, num_bits, 
		port="COM18", control_mode='uart', read_mode='uart',
		psu_name='USB0::0x0957::0x2C07::MY57801384::0::INSTR',
		iterations=1, gpio_settings=dict()):
	"""
	Inputs:
		vin_min/max: Float. The min and max voltages to feed to the
			ADC.
		num_bits: Integer. The bit resolution of the ADC.
		port: String. Name of the COM port that the UART or the reading Teensy
			is connected to.
		control_mode: String 'uart' or 'gpio'. Determines if you'll be triggering
			the FSM via UART or GPIO.
		read_mode: String 'uart' or 'gpio'. Determines if you're reading via GPIO.
		psu_name: String. Name to use in the connection for the 
			waveform generator.
		iterations: Integer. Number of times to take a reading for
			a single input voltage.
		gpio_settings: Dictionary with the following key:value pairings
			adc_settle_cycles: Integer [1,256]. Unclear what the control in the original 
				FSM is, but this will dictate the number of microseconds to wait 
				for the ADC to settle. Applicable only if the Teensy is used rather than
				UART.
			pga_bypass: Integer 0 or 1. 1 = bypass the PGA, otherwise use the PGA. Applicable
				only if the Teensy is used rather than UART.
			pga_settle_us: Integer. Number of microseconds to wait for the PGA output to
				to settle. Applicable only if the Teensy is used rather than
				UART.
	Outputs:
		Returns a collection of endpoint INLs taken from vin_min 
		up to vin_max. Note that this assumes that SCM has already 
		been programmed.
	"""
	# Constructing the vector of input voltages to give the ADC
	vlsb_ideal = (vin_max-vin_min)/2**num_bits
	vin_vec = np.arange(vin_min, vin_max, vlsb_ideal/4)

	# Running the test
	adc_outs = test_adc_psu(vin_vec, port, control_mode, read_mode, \
							psu_name, iterations, gpio_settings)

	return calc_adc_inl_endpoint(adc_outs, vlsb_ideal)


if __name__ == "__main__":
	programmer_port = "COM15"
	scm_port = "COM24"

	### Testing programming the cortex ###
	if True:
		program_cortex_specs = dict(teensy_port=programmer_port,
									uart_port=scm_port,
									file_binary="../code.bin",
									boot_mode="optical",
									skip_reset=False,
									insert_CRC=True,
									pad_random_payload=False,)
		program_cortex(**program_cortex_specs)

	### Running a spot check with the ADC using the 	 ###
	### UART to trigger the software-driven FSM triggers ###
	if False:
		test_adc_spot_loopback_specs = dict(
			port=scm_port,
			iterations=10)

		test_adc_spot_loopback(**test_adc_spot_loopback_specs)

	### Running a spot check with the ADC using the UART ###
	### to trigger a single reading. 					 ###
	if False:
		test_adc_spot_specs = dict(
			port=scm_port,
			iterations=50)
		adc_out = test_adc_spot(**test_adc_spot_specs)
		print(adc_out)
		adc_out_dict = dict()
		adc_out_dict[0.0] = adc_out

		fname = './data/spot_check.csv'
		write_adc_data(adc_out_dict, fname)

	### Running many iterations on a large ###
	### sweep. ###
	if True:
		test_adc_psu_specs = dict(vin_vec=np.arange(0, 1.0, 0.1e-3),
								port=scm_port,
								control_mode='uart',
								read_mode='uart',
								psu_name='USB0::0x0957::0x2C07::MY57801384::0::INSTR',
								iterations=50,
								gpio_settings=dict())
		adc_outs = test_adc_psu(**test_adc_psu_specs)

		ts = time.gmtime()
		datetime = time.strftime("%Y%m%d_%H%M%S",ts)
		write_adc_data(adc_outs, './data/psu_{}'.format(datetime))

	### Reading in data from a file and plotting appropriately ###
	if False:
		fname = "./psu_20190809_032357.csv"

		plot_adc_data_specs = dict(adc_outs=read_adc_data(fname),
								plot_inl=False,
								plot_ideal=True,
								vdd=0.8,
								num_bits=10)
		plot_adc_data(**plot_adc_data_specs)

	if False:
		file_noise = './data/psu_run_noise.csv'
		adc_noise = read_adc_data(file_noise)

		for vin in adc_noise.keys():
			plt.figure()
			plt.hist(adc_noise[vin], bins=100)
			plt.title("Vin={} V".format(vin))
			plt.xlabel("Code")
			plt.ylabel("Occurrences")
			plt.show()