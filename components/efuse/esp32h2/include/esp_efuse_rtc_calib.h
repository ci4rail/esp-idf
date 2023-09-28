/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <esp_types.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

//This is the ADC calibration value version burnt in efuse
#define ESP_EFUSE_ADC_CALIB_VER      1
#define ESP_EFUSE_ADC_CALIB_VER_MIN  ESP_EFUSE_ADC_CALIB_VER
#define ESP_EFUSE_ADC_CALIB_VER_MAX  ESP_EFUSE_ADC_CALIB_VER

/**
 * @brief Get the RTC calibration efuse version
 *
 * @return Version of the stored efuse
 */
int esp_efuse_rtc_calib_get_ver(void);

/**
 * @brief Get the init code in the efuse, for the corresponding attenuation.
 *
 * @param version   Version of the stored efuse
 * @param adc_unit  ADC unit. Not used, for compatibility. On esp32h2, for calibration v1, both ADC units use the same init code (calibrated by ADC1)
 * @param atten     Attenuation of the init code
 * @return The init code stored in efuse
 */
uint32_t esp_efuse_rtc_calib_get_init_code(int version, uint32_t adc_unit, int atten);

/**
 * @brief Get the calibration digits stored in the efuse, and the corresponding voltage.
 *
 * @param version Version of the stored efuse
 * @param atten         Attenuation to use
 * @param out_digi      Output buffer of the digits
 * @param out_vol_mv    Output of the voltage, in mV
 * @return
 *      - ESP_ERR_INVALID_ARG: If efuse version or attenuation is invalid
 *      - ESP_OK: if success
 */
esp_err_t esp_efuse_rtc_calib_get_cal_voltage(int version, int atten, uint32_t* out_digi, uint32_t* out_vol_mv);

/**
 * @brief Get the temperature sensor calibration number delta_T stored in the efuse.
 *
 * @param tsens_cal Pointer of the specification of temperature sensor calibration number in efuse.
 *
 * @return ESP_OK if get the calibration value successfully.
 *         ESP_ERR_INVALID_ARG if can't get the calibration value.
 */
esp_err_t esp_efuse_rtc_calib_get_tsens_val(float* tsens_cal);

#ifdef __cplusplus
}
#endif
