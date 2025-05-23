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
    const struct adc_dt_spec adc_channel0;
    const struct adc_dt_spec adc_channel1;
    const uint16_t steps;
};

struct ploopy_optical_encoder_data {
    const struct sensor_trigger *trigger;
    sensor_trigger_handler_t handler;

    struct adc_sequence adc_seq;

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

    int err = adc_read_dt(&config->adc_channel0, &data->adc_seq);
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
    data->adc_seq.buffer = data->samples;
    data->adc_seq.buffer_size = sizeof(data->samples);

    __ASSERT(config->adc_channel0.dev == config->adc_channel1.dev,
        "Both ADC channels must be on the same device.");

    adc_sequence_init_dt(&config->adc_channel0, &data->adc_seq);
    data->adc_seq.channels |= BIT(config->adc_channel1.channel_id);

    if (!adc_is_ready_dt(&config->adc_channel0)) {
        LOG_ERR("ADC device is not ready");
        return -EINVAL;
    }

    err = adc_channel_setup_dt(&config->adc_channel0);
    if (err) {
        LOG_ERR("failed to configure ADC channel 0 (err %d)",
            err);
        return err;
    }
    err = adc_channel_setup_dt(&config->adc_channel1);
    if (err) {
        LOG_ERR("failed to configure ADC channel 1 (err %d)",
            err);
        return err;
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
        .adc_channel0 = ADC_DT_SPEC_INST_GET_BY_IDX(n, 0),                                          \
        .adc_channel1 = ADC_DT_SPEC_INST_GET_BY_IDX(n, 1),                                          \
        .steps = DT_INST_PROP(n, steps),                                                            \
    };                                                                                              \
                                                                                                    \
    DEVICE_DT_INST_DEFINE(n, ploopy_optical_encoder_init, NULL, &ploopy_optical_encoder_data_##n, &ploopy_optical_encoder_cfg_##n,           \
                          POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, &ploopy_optical_encoder_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PLOOPY_OPTICAL_ENCODER_INST)