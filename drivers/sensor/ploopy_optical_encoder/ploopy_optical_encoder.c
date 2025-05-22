/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT ploopy_optical_encoder

#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>

LOG_MODULE_REGISTER(optical_encoder, CONFIG_SENSOR_LOG_LEVEL);

#define FULL_ROTATION 360
#define ADC_DELTA_THRESHOLD 128

struct ploopy_optical_encoder_config {
    uint16_t startup_delay;
    uint16_t poll_period;
    const struct device *adc;
    uint8_t adc_ch0;
    uint8_t adc_ch1;
    struct adc_sequence adc_seq;
    const uint16_t steps;
};

struct ploopy_optical_encoder_data {
    const struct sensor_trigger *trigger;
    sensor_trigger_handler_t handler;

    size_t event_index;
    struct k_work_delayable work;
    const struct device *dev;

    int16_t prev_samples[2];
    int16_t samples[2];
    uint8_t state;

    int8_t pulses;
    int8_t delta;
};

static void ploopy_optical_encoder_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
    struct ploopy_optical_encoder_data *data = CONTAINER_OF(dwork, struct ploopy_optical_encoder_data, work);
    const struct device *dev = data->dev;
    const struct ploopy_optical_encoder_config *config = dev->config;

    int err = adc_read(config->adc, &config->adc_seq);
    if (err) {
        LOG_ERR("failed to read ADC channels (err %d)", err);
    }
    else {
        uint8_t last_state = data->state & 0b11;

        // Very simplistic, if a sample increases by more than ADC_DELTA_THRESHOLD set it high, if it drops by ADC_DELTA_THRESHOLD, set it low.
        const int16_t delta1 = data->samples[0] - data->prev_samples[0];
        const int16_t delta2 = data->samples[1] - data->prev_samples[1];
        data->prev_samples[0] = data->samples[0];
        data->prev_samples[1] = data->samples[1];

        if (delta1 > ADC_DELTA_THRESHOLD) data->state |= 0b10;
        if (delta1 < -ADC_DELTA_THRESHOLD) data->state &= 0b01;
        if (delta2 > ADC_DELTA_THRESHOLD) data->state |= 0b01;
        if (delta2 < -ADC_DELTA_THRESHOLD) data->state &= 0b10;

        if (data->state != last_state) {
            const uint8_t transiton = (last_state << 2) | data->state;
            switch (transiton) {
                case 0b0010:
                case 0b0100:
                case 0b1101:
                case 0b1011:
                    data->delta = -1;
                    break;
                case 0b0001:
                case 0b0111:
                case 0b1110:
                case 0b1000:
                    data->delta = 1;
                    break;
                default:
                    data->delta = 0;
                    break;
                }
            if (data->delta) {
                data->pulses += data->delta;
                data->handler(dev, data->trigger);
            }
        }
    }

    // Poll again
    k_work_schedule(&data->work, K_MSEC(config->poll_period));
}

static int ploopy_optical_encoder_trigger_set(const struct device *dev, const struct sensor_trigger *trig,
                                sensor_trigger_handler_t handler) {
    struct ploopy_optical_encoder_data *data = dev->data;
    const struct ploopy_optical_encoder_config *config = dev->config;

    data->trigger = trig;
    data->handler = handler;

    int ret = k_work_schedule(&data->work, K_MSEC(config->startup_delay));
    if (ret < 0) {
        LOG_WRN("Failed to schedule an optical scroll wheel poll %d", ret);
        return ret;
    }

    return 0;
}

static int ploopy_optical_encoder_sample_fetch(const struct device *dev, enum sensor_channel chan) {
    return 0;
}

static int ploopy_optical_encoder_channel_get(const struct device *dev, enum sensor_channel chan,
                                struct sensor_value *val) {
    struct ploopy_optical_encoder_data *data = dev->data;
    const struct ploopy_optical_encoder_config *config = dev->config;
    const int32_t pulses = data->pulses;

    if (chan != SENSOR_CHAN_ROTATION) {
        return -ENOTSUP;
    }

    data->pulses = 0;

    val->val1 = (pulses * FULL_ROTATION) / config->steps;
    val->val2 = (pulses * FULL_ROTATION) % config->steps;
    if (val->val2 != 0) {
        val->val2 *= 1000000;
        val->val2 /= config->steps;
    }

    return 0;
}

static const struct sensor_driver_api ploopy_optical_encoder_driver_api = {
    .trigger_set = ploopy_optical_encoder_trigger_set,
    .sample_fetch = ploopy_optical_encoder_sample_fetch,
    .channel_get = ploopy_optical_encoder_channel_get,
};

int ploopy_optical_encoder_init(const struct device *dev) {
    const struct ploopy_optical_encoder_config *config = dev->config;
    struct ploopy_optical_encoder_data *data = dev->data;
    int err = 0;

    data->dev = dev;
    data->event_index = -1;

    const struct adc_channel_cfg ch_cfg[] = {
        {
            .gain = ADC_GAIN_1,
            .reference = ADC_REF_INTERNAL,
            .acquisition_time = ADC_ACQ_TIME_DEFAULT,
            .channel_id = config->adc_ch0,
            .differential = 0,
        },
        {
            .gain = ADC_GAIN_1,
            .reference = ADC_REF_INTERNAL,
            .acquisition_time = ADC_ACQ_TIME_DEFAULT,
            .channel_id = config->adc_ch1,
            .differential = 0,
        },
    };

    if (!device_is_ready(config->adc)) {
        LOG_ERR("ADC device is not ready");
        return -EINVAL;
    }

    for (int i = 0; i < ARRAY_SIZE(ch_cfg); i++) {
        err = adc_channel_setup(config->adc, &ch_cfg[i]);
        if (err) {
            LOG_ERR("failed to configure ADC channel (err %d)",
                err);
            return err;
        }
    }

    k_work_init_delayable(&data->work, ploopy_optical_encoder_work_cb);

    return err;
}

#define PLOOPY_OPTICAL_ENCODER_INST(n)                                                              \
    static struct ploopy_optical_encoder_data ploopy_optical_encoder_data_##n = {                   \
    };                                                                                              \
    static const struct ploopy_optical_encoder_config ploopy_optical_encoder_cfg_##n = {            \
        .startup_delay = DT_INST_PROP(n, event_startup_delay),                                      \
        .poll_period = DT_INST_PROP(n, poll_period),                                                \
        .adc =  DEVICE_DT_GET(DT_INST_IO_CHANNELS_CTLR(n)),                                         \
        .adc_ch0 =  DT_INST_IO_CHANNELS_INPUT_BY_IDX(n, 0),                                         \
        .adc_ch1 =  DT_INST_IO_CHANNELS_INPUT_BY_IDX(n, 1),                                         \
        .adc_seq =                                                                                  \
        {                                                                                           \
            .channels = BIT(DT_INST_IO_CHANNELS_INPUT_BY_IDX(n, 0)) |                               \
                        BIT(DT_INST_IO_CHANNELS_INPUT_BY_IDX(n, 1)),                                \
            .buffer = &ploopy_optical_encoder_data_##n.samples,                                     \
            .buffer_size = sizeof(ploopy_optical_encoder_data_##n.samples),                         \
            .resolution = 12U,                                                                      \
        },                                                                                          \
        .steps = DT_INST_PROP(n, steps),                                                            \
    };                                                                                              \
                                                                                                    \
    DEVICE_DT_INST_DEFINE(n, ploopy_optical_encoder_init, NULL, &ploopy_optical_encoder_data_##n, &ploopy_optical_encoder_cfg_##n,           \
                          POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, &ploopy_optical_encoder_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PLOOPY_OPTICAL_ENCODER_INST)