/*
 * Regulator driver for ST's PWM Regulators
 *
 * Copyright (C) 2014 - STMicroelectronics Inc.
 *
 * Author: Lee Jones <lee.jones@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pwm.h>
#include <linux/delay.h>

#define ST_MAX_PWM_CODE		255

struct st_pwm_regulator_data {
	struct regulator_dev *regulator;
	struct pwm_device *pwm;
	bool enabled;
	int volt_uV;
};

static int st_pwm_voltage_to_duty_cycle(struct regulator_dev *regdev,
					int volt_mV)
{
	int a;
	int pwm_code;
	int min_mV = regdev->constraints->min_uV / 1000;
	int max_mV = regdev->constraints->max_uV / 1000;
	int vdiff = min_mV - max_mV;

	a = ST_MAX_PWM_CODE - min_mV * ST_MAX_PWM_CODE / vdiff;
	pwm_code = a + ST_MAX_PWM_CODE * volt_mV / vdiff;

	if (pwm_code < 0)
		pwm_code = 0;
	if (pwm_code > ST_MAX_PWM_CODE)
		pwm_code = ST_MAX_PWM_CODE;

	return pwm_code * 100 / ST_MAX_PWM_CODE;
}

static int st_pwm_regulator_get_voltage(struct regulator_dev *regdev)
{
	struct st_pwm_regulator_data *drvdata = rdev_get_drvdata(regdev);

	return drvdata->volt_uV;
}

static int st_pwm_regulator_set_voltage(struct regulator_dev *regdev,
					int min_uV,
					int max_uV,
					unsigned *selector)
{
	int ret;
	struct st_pwm_regulator_data *drvdata = rdev_get_drvdata(regdev);
	int duty_cycle = st_pwm_voltage_to_duty_cycle(regdev, min_uV / 1000);

	ret = pwm_config(drvdata->pwm,
			 (drvdata->pwm->period / 100) * duty_cycle,
			 drvdata->pwm->period);
	if (ret) {
		dev_err(&regdev->dev, "Failed to configure PWM\n");
		return ret;
	}

	ret = pwm_enable(drvdata->pwm);
	if (ret) {
		dev_err(&regdev->dev, "Failed to enable PWM\n");
		return ret;
	}
	drvdata->volt_uV = min_uV;

	/*
	 * Delay required by PWM regulator to settle to the new voltage.
	 * TODO: Calibrate this delay w.r.t. to the delay introduced
	 * by body biasing
	 */
	usleep_range(10000, 10050);

	return 0;
}

static struct regulator_ops st_pwm_regulator_voltage_ops = {
	.get_voltage = st_pwm_regulator_get_voltage,
	.set_voltage = st_pwm_regulator_set_voltage,
};

static struct of_device_id st_pwm_of_match[] = {
	{ .compatible = "st,pwm-regulator" },
	{ },
};
MODULE_DEVICE_TABLE(of, st_pwm_of_match);

static int st_pwm_regulator_probe(struct platform_device *pdev)
{
	struct regulator_desc *desc;
	struct st_pwm_regulator_data *drvdata;
	struct regulator_config config = { };
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	if (!np) {
		dev_err(dev, "Device Tree node missing\n");
		return -EINVAL;
	}

	desc = devm_kzalloc(dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	desc->name = dev_name(dev);
	desc->ops = &st_pwm_regulator_voltage_ops,
	desc->type = REGULATOR_VOLTAGE,
	desc->owner = THIS_MODULE,
	desc->supply_name = "pwm",
	desc->continuous_voltage_range = true,

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	config.init_data = of_get_regulator_init_data(dev, np, desc);
	if (!config.init_data)
		return -ENOMEM;

	config.of_node = np;
	config.dev = dev;
	config.driver_data = drvdata;

	drvdata->pwm = devm_pwm_get(dev, NULL);
	if (IS_ERR(drvdata->pwm)) {
		dev_err(dev, "Failed to get PWM\n");
		return PTR_ERR(drvdata->pwm);
	}

	drvdata->regulator = regulator_register(desc, &config);
	if (IS_ERR(drvdata->regulator)) {
		dev_err(dev, "Failed to register regulator %s\n",
			desc->name);
		return PTR_ERR(drvdata->regulator);
	}

	platform_set_drvdata(pdev, drvdata);

	return 0;
}

static int st_pwm_regulator_remove(struct platform_device *pdev)
{
	struct st_pwm_regulator_data *drvdata = platform_get_drvdata(pdev);

	regulator_unregister(drvdata->regulator);

	return 0;
}

static struct platform_driver st_pwm_regulator_driver = {
	.driver = {
		.name		= "st-pwm-regulator",
		.owner		= THIS_MODULE,
		.of_match_table = of_match_ptr(st_pwm_of_match),
	},
	.probe = st_pwm_regulator_probe,
	.remove = st_pwm_regulator_remove,
};

module_platform_driver(st_pwm_regulator_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lee Jones <lee.jones@linaro.org>");
MODULE_DESCRIPTION("ST PWM Regulator Driver");
MODULE_ALIAS("platform:st_pwm-regulator");
