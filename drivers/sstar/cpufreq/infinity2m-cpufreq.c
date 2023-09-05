/*
* infinity6-cpufreq.c- Sigmastar
*
* Copyright (C) 2018 Sigmastar Technology Corp.
*
* Author: karl.xiao <karl.xiao@sigmastar.com.tw>
*
* This software is licensed under the terms of the GNU General Public
* License version 2, as published by the Free Software Foundation, and
* may be copied, distributed, and modified under those terms.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
*/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/version.h>

#include "ms_types.h"
#include "ms_platform.h"
#include "registers.h"

u8 enable_scaling_voltage = 1;
u32 sidd_th_100x = 1243;  //sidd threshold=12.74mA
extern int g_sCurrentTemp;
extern int g_sCurrentTempThreshLo;
extern int g_sCurrentTempThreshHi;
struct timer_list timer_temp;

extern unsigned int gpio_vid_width;
extern unsigned int *gpio_vid_pins;
extern unsigned int *gpio_vid_voltages;

extern int Chip_Get_Package_Type(void);

extern void ms_cpuclk_dvfs_disable(void);

static int ms_cpufreq_verify(struct cpufreq_policy *policy)
{
    if (policy->cpu)
        return -EINVAL;

    cpufreq_verify_within_cpu_limits(policy);
    return 0;
}

static int ms_cpufreq_target(struct cpufreq_policy *policy, unsigned int target_freq, unsigned int relation)
{
    struct cpufreq_freqs freqs;
    int ret;

    freqs.old = policy->cur;
    freqs.new = target_freq;

	if (freqs.old == freqs.new)
		return 0;

    cpufreq_freq_transition_begin(policy, &freqs);
    ret = clk_set_rate(policy->clk, target_freq * 1000);
    cpufreq_freq_transition_end(policy, &freqs, ret);

    return ret;
}

static ssize_t show_scaling_voltage(struct kobject *kobj, struct attribute *attr, char *buf)
{
    char *str = buf;
    char *end = buf + PAGE_SIZE;

    str += scnprintf(str, end - str, "%d\n", enable_scaling_voltage);

    return (str - buf);
}

static ssize_t store_scaling_voltage(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
    u32 enable;
    if (sscanf(buf, "%d", &enable)<=0)
        return 0;

    if(enable)
    {
        enable_scaling_voltage=1;
        pr_info("[CPUFREQ] Enable voltage-scaling\n");
    }
    else
    {
        enable_scaling_voltage=0;
        pr_info("[CPUFREQ] Disable voltage-scaling\n");
    }
    return count;
}
define_one_global_rw(scaling_voltage);

void set_core_voltage (int vcore);
extern int g_sCurrentVoltageCore;

static ssize_t show_voltage_current(struct kobject *kobj, struct attribute *attr, char *buf)
{
    char *str = buf;
    char *end = buf + PAGE_SIZE;

    str += scnprintf(str, end - str, "%d\n", g_sCurrentVoltageCore);

    return (str - buf);
}

static ssize_t store_voltage_current(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
    u32 voltage = 0;
    if (sscanf(buf, "%d", &voltage)<=0)
        return 0;

    set_core_voltage(voltage);

    return count;
}
define_one_global_rw(voltage_current);

static ssize_t show_voltage_available(struct kobject *kobj, struct attribute *attr, char *buf)
{
    char *str = buf;
    char *end = buf + PAGE_SIZE;
    int voltage_lv_cnt = 0;

    if(gpio_vid_width > 0)
    {
        for (voltage_lv_cnt = 0; voltage_lv_cnt < (1<<gpio_vid_width); voltage_lv_cnt++)
        {
            str += scnprintf(str, end - str, "%d ", gpio_vid_voltages[voltage_lv_cnt]);
        }
    }

    str += scnprintf(str, end - str, "\n");

    return (str - buf);
}
define_one_global_ro(voltage_available);

static ssize_t show_cpufreq_testout(struct kobject *kobj, struct attribute *attr, char *buf)
{
    char *str = buf;
    char *end = buf + PAGE_SIZE;
    u16 reg_value = 0;
    u32 freq = 0;

    if( (INREG8(BASE_REG_RIU_PA + (0x102216 << 1))&BIT0) != BIT0)
    {
        OUTREG8(BASE_REG_RIU_PA + (0x102216 << 1), 0x01);
        OUTREG8(BASE_REG_RIU_PA + (0x101EEE << 1), 0x04);
        OUTREG8(BASE_REG_RIU_PA + (0x101EEA << 1), 0x04);
        OUTREG8(BASE_REG_RIU_PA + (0x101EEA << 1)+1, 0x40);
        OUTREG8(BASE_REG_RIU_PA + (0x101EEC << 1), 0x01);
        OUTREG8(BASE_REG_RIU_PA + (0x101EE0 << 1)+1, 0x00);
        OUTREG8(BASE_REG_RIU_PA + (0x101EE0 << 1)+1, 0x80);
        udelay(100);
    }
    reg_value = (INREG8(BASE_REG_RIU_PA + (0x101EE2 << 1)) | (INREG8(BASE_REG_RIU_PA + (0x101EE2 << 1)+1)<<8));
    //freq = (reg_value * 4000)/83333;
    freq = reg_value * 48000;

    str += scnprintf(str, end - str, "%d\n", freq);

    return (str - buf);
}

static ssize_t store_cpufreq_testout(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
    u32 enable;
    if (sscanf(buf, "%d", &enable)<=0)
        return 0;

    if(enable)
    {
        pr_info("[CPUFREQ] Enable CPU freq testout\n");
        OUTREG8(BASE_REG_RIU_PA + (0x102216 << 1), 0x01);
        OUTREG8(BASE_REG_RIU_PA + (0x101EEE << 1), 0x04);
        OUTREG8(BASE_REG_RIU_PA + (0x101EEA << 1), 0x04);
        OUTREG8(BASE_REG_RIU_PA + (0x101EEA << 1)+1, 0x40);
        OUTREG8(BASE_REG_RIU_PA + (0x101EEC << 1), 0x01);
        OUTREG8(BASE_REG_RIU_PA + (0x101EE0 << 1)+1, 0x00);
        OUTREG8(BASE_REG_RIU_PA + (0x101EE0 << 1)+1, 0x80);
    }
    else
    {
        pr_info("[CPUFREQ] Disable CPU freq testout\n");
        OUTREG8(BASE_REG_RIU_PA + (0x102216 << 1), 0x00);
        OUTREG8(BASE_REG_RIU_PA + (0x101EEE << 1), 0x00);
        OUTREG8(BASE_REG_RIU_PA + (0x101EEA << 1), 0x00);
        OUTREG8(BASE_REG_RIU_PA + (0x101EEA << 1)+1, 0x00);
        OUTREG8(BASE_REG_RIU_PA + (0x101EEC << 1), 0x00);
        OUTREG8(BASE_REG_RIU_PA + (0x101EE0 << 1)+1, 0x00);
    }

    return count;
}
define_one_global_rw(cpufreq_testout);


static ssize_t show_vid_gpio_map(struct kobject *kobj, struct attribute *attr, char *buf)
{
    char *str = buf;
    char *end = buf + PAGE_SIZE;
    unsigned int i = 0;

    if(gpio_vid_width > 0)
    {
        for (i = 0; i < gpio_vid_width; i++)
        {
            str += scnprintf(str, end - str, "vid_%d=%d ", i, gpio_vid_pins[i]);
        }
    }

    str += scnprintf(str, end - str, "\n");

    return (str - buf);
}
define_one_global_ro(vid_gpio_map);

static ssize_t show_temp_out(struct kobject *kobj, struct attribute *attr, char *buf)
{
    char *str = buf;
    char *end = buf + PAGE_SIZE;

    str += scnprintf(str, end - str, "Temp=%d\n", g_sCurrentTemp);

    return (str - buf);
}
define_one_global_ro(temp_out);


static ssize_t show_temp_adjust_threshold_lo(struct kobject *kobj, struct attribute *attr, char *buf)
{
    char *str = buf;
    char *end = buf + PAGE_SIZE;

    str += scnprintf(str, end - str, "%d\n", g_sCurrentTempThreshLo);

    return (str - buf);
}
static ssize_t store_temp_adjust_threshold_lo(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
    u32 value;
    if (sscanf(buf, "%d", &value)<=0)
        return 0;
    g_sCurrentTempThreshLo = value;
    return count;
}
define_one_global_rw(temp_adjust_threshold_lo);

static ssize_t show_temp_adjust_threshold_hi(struct kobject *kobj, struct attribute *attr, char *buf)
{
    char *str = buf;
    char *end = buf + PAGE_SIZE;

    str += scnprintf(str, end - str, "%d\n", g_sCurrentTempThreshHi);

    return (str - buf);
}
static ssize_t store_temp_adjust_threshold_hi(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
    u32 value;
    if (sscanf(buf, "%d", &value)<=0)
        return 0;
    g_sCurrentTempThreshHi = value;
    return count;
}
define_one_global_rw(temp_adjust_threshold_hi);

int ms_get_temp(void)
{
    int vbe_code_ft;
    int vbe_code;

    CLRREG16(BASE_REG_PMSAR_PA + REG_ID_19, BIT6); //ch7 reference voltage select to 2.0V
    CLRREG16(BASE_REG_PMSAR_PA + REG_ID_10, BIT0); //reg_pm_dmy
    SETREG16(BASE_REG_PMSLEEP_PA + REG_ID_64, BIT10);
    SETREG16(BASE_REG_PMSLEEP_PA + REG_ID_2F, BIT2);
    OUTREG16(BASE_REG_PMSAR_PA + REG_ID_00, 0xA20);
    SETREG16(BASE_REG_PMSAR_PA + REG_ID_00, BIT14);
    vbe_code = INREG16(BASE_REG_PMSAR_PA + REG_ID_46);
    vbe_code_ft = INREGMSK16(BASE_REG_EFUSE_PA + REG_ID_09, 0x3FF);

    if (vbe_code_ft == 0)   // if no trim info
        vbe_code_ft = 400;

    //GF28LP equation to calculate temperature
    return (1270 * (vbe_code_ft - vbe_code) + 29000)/1000;
}

void monitor_temp_timer_handler(unsigned long value)
{

    g_sCurrentTemp = ms_get_temp();
    mod_timer(&timer_temp, jiffies + HZ);
}

static int ms_cpufreq_init(struct cpufreq_policy *policy)
{
    //int reg, sidd;
    //int package = Chip_Get_Package_Type();

    if (policy->cpu != 0)
        return -EINVAL;

    policy->min = 800000;
    policy->max = 800000;

    policy->cpuinfo.min_freq = 400000;
    policy->cpuinfo.max_freq = 1200000;
    policy->cpuinfo.transition_latency = 100000;
    policy->clk = of_clk_get(of_find_node_by_type(NULL, "cpu"), 0);

    //create a timer for monitor temperature
    init_timer(&timer_temp);
    timer_temp.function = monitor_temp_timer_handler;
    timer_temp.expires = jiffies + HZ/10;
    ms_get_temp(); //We will update temperature after Hz/10ms. Drop first value due to one adc need cost 8ch*8.9usec.
    add_timer(&timer_temp);

    pr_info("[%s] cpu current clk=%lu\n", __func__, clk_get_rate(policy->clk));


    return 0;
}

static int ms_cpufreq_exit(struct cpufreq_policy *policy)
{
    if (policy && !IS_ERR(policy->clk))
        clk_put(policy->clk);

    //delete a timer for monitor temperature
    del_timer_sync(&timer_temp);

    return 0;
}

static struct cpufreq_driver ms_cpufreq_driver = {
    .verify = ms_cpufreq_verify,
    .target = ms_cpufreq_target,
    .get    = cpufreq_generic_get,
    .init   = ms_cpufreq_init,
    .exit   = ms_cpufreq_exit,
    .name   = "Mstar cpufreq",
};

static int ms_cpufreq_probe(struct platform_device *pdev)
{
    int ret;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0)
    ret = cpufreq_sysfs_create_file(&scaling_voltage.attr);
    ret |= cpufreq_sysfs_create_file(&cpufreq_testout.attr);
    ret |= cpufreq_sysfs_create_file(&vid_gpio_map.attr);
    ret |= cpufreq_sysfs_create_file(&temp_adjust_threshold_lo.attr);
    ret |= cpufreq_sysfs_create_file(&temp_adjust_threshold_hi.attr);
    ret |= cpufreq_sysfs_create_file(&temp_out.attr);
    ret |= cpufreq_sysfs_create_file(&voltage_current.attr);
    ret |= cpufreq_sysfs_create_file(&voltage_available.attr);
#else
    ret = sysfs_create_file(cpufreq_global_kobject, &scaling_voltage.attr);
    ret |= sysfs_create_file(cpufreq_global_kobject, &cpufreq_testout.attr);
    ret |= sysfs_create_file(cpufreq_global_kobject, &vid_gpio_map.attr);
    ret |= sysfs_create_file(cpufreq_global_kobject, &temp_adjust_threshold_lo.attr);
    ret |= sysfs_create_file(cpufreq_global_kobject, &temp_adjust_threshold_hi.attr);
    ret |= sysfs_create_file(cpufreq_global_kobject, &temp_out.attr);
    ret |= sysfs_create_file(cpufreq_global_kobject, &voltage_current.attr);
    ret |= sysfs_create_file(cpufreq_global_kobject, &voltage_available.attr);
#endif

    if (ret)
    {
        pr_err("[%s] cpufreq_sysfs_create_file failed\n", __func__);
    }

    return cpufreq_register_driver(&ms_cpufreq_driver);
}

static int ms_cpufreq_remove(struct platform_device *pdev)
{
    return cpufreq_unregister_driver(&ms_cpufreq_driver);
}

static const struct of_device_id ms_cpufreq_of_match_table[] = {
    { .compatible = "sstar,infinity-cpufreq" },
    {}
};
MODULE_DEVICE_TABLE(of, ms_cpufreq_of_match_table);

static struct platform_driver ms_cpufreq_platdrv = {
    .driver = {
        .name  = "ms_cpufreq",
        .owner = THIS_MODULE,
        .of_match_table = ms_cpufreq_of_match_table,
    },
    .probe  = ms_cpufreq_probe,
    .remove = ms_cpufreq_remove,
};

module_platform_driver(ms_cpufreq_platdrv);
