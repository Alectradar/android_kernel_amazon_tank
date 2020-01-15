/* Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*****************************************************************************
 *                     C O M P I L E R   F L A G S
 *****************************************************************************/


/*****************************************************************************
 *                E X T E R N A L   R E F E R E N C E S
 *****************************************************************************/
#include <linux/dma-mapping.h>
#include "mt_soc_pcm_common.h"
#include "mt_soc_digital_type.h"

/* information about */
static struct AFE_MEM_CONTROL_T *Bt_Dai_Control_context;
static struct snd_dma_buffer *Bt_Dai_Capture_dma_buf;

static DEFINE_SPINLOCK(auddrv_BTDaiInCtl_lock);

/*
 *    function implementation
 */
static void StartAudioBtDaiHardware(struct snd_pcm_substream *substream);
static void StopAudioBtDaiHardware(struct snd_pcm_substream *substream);
static int mtk_bt_dai_probe(struct platform_device *pdev);
static int mtk_bt_dai_pcm_close(struct snd_pcm_substream *substream);
static int mtk_asoc_bt_dai_pcm_new(struct snd_soc_pcm_runtime *rtd);
static int mtk_asoc_bt_dai_probe(struct snd_soc_platform *platform);

#ifdef BTDAI_MODEM_INTF
static struct AudioDigitalPCM extmd_pcm_rx = {
	.mSyncOutInv = Soc_Aud_INV_SYNC_NO_INVERSE,
	.mBclkOutInv = Soc_Aud_INV_BCK_NO_INVERSE,
	.mSyncInInv = Soc_Aud_INV_SYNC_NO_INVERSE,
	.mBclkInInv = Soc_Aud_INV_BCK_INVESE,
	.mTxLchRepeatSel = Soc_Aud_TX_LCH_RPT_TX_LCH_NO_REPEAT,
	.mVbt16kModeSel = Soc_Aud_VBT_16K_MODE_VBT_16K_MODE_DISABLE,
	.mExtModemSel = Soc_Aud_EXT_MODEM_MODEM_2_USE_EXTERNAL_MODEM,
	.mExtendBckSyncLength = 0,
	.mExtendBckSyncTypeSel = Soc_Aud_PCM_SYNC_TYPE_BCK_CYCLE_SYNC,
	.mSingelMicSel = Soc_Aud_BT_MODE_DUAL_MIC_ON_TX,
	.mAsyncFifoSel = Soc_Aud_BYPASS_SRC_SLAVE_USE_ASRC,
	.mSlaveModeSel = Soc_Aud_PCM_CLOCK_SOURCE_SALVE_MODE,
	.mPcmWordLength = Soc_Aud_PCM_WLEN_LEN_PCM_16BIT,
	.mPcmModeWidebandSel = Soc_Aud_PCM_MODE_PCM_MODE_8K,
	.mPcmFormat = Soc_Aud_PCM_FMT_PCM_MODE_A,
	.mModemPcmOn = false,
};
#endif

static struct snd_pcm_hardware mtk_btdai_hardware = {

	.info = (SNDRV_PCM_INFO_INTERLEAVED),
	.formats = SND_SOC_STD_MT_FMTS,
	.rates = SOC_NORMAL_USE_RATE,
	.rate_min = SOC_NORMAL_USE_RATE_MIN,
	.rate_max = SOC_NORMAL_USE_RATE_MAX,
	.channels_min = SOC_NORMAL_USE_CHANNELS_MIN,
	.channels_max = SOC_NORMAL_USE_CHANNELS_MAX,
	.buffer_bytes_max = BT_DAI_MAX_BUFFER_SIZE,
	.period_bytes_max = BT_DAI_MAX_BUFFER_SIZE,
	.periods_min = SOC_NORMAL_USE_PERIODS_MIN,
	.periods_max = SOC_NORMAL_USE_PERIODS_MAX,
	.fifo_size = 0,
};

static void StopAudioBtDaiHardware(struct snd_pcm_substream *substream)
{
	pr_debug("StopAudioBtDaiHardware\n");

	/* here to set interrupt */
	mt_afe_set_irq_state(Soc_Aud_IRQ_MCU_MODE_IRQ2_MCU_MODE, false);

	/* here to turn off digital part */
#ifdef BTDAI_MODEM_INTF
	/*external modem pcm in --> mod_dai*/
	mt_afe_set_connection(Soc_Aud_InterCon_DisConnect, Soc_Aud_InterConnectionInput_I09,
		Soc_Aud_InterConnectionOutput_O12);
#else
	mt_afe_set_connection(Soc_Aud_InterCon_DisConnect, Soc_Aud_InterConnectionInput_I02,
		      Soc_Aud_InterConnectionOutput_O11);
#endif

	mt_afe_enable_afe(false);
}

static void StartAudioBtDaiHardware(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	pr_debug("StartAudioBtDaiHardware period_size = %d\n",
	       (unsigned int)(substream->runtime->period_size));

	/* here to set interrupt */
	mt_afe_set_irq_counter(Soc_Aud_IRQ_MCU_MODE_IRQ2_MCU_MODE, substream->runtime->period_size >> 1);
	mt_afe_set_irq_rate(Soc_Aud_IRQ_MCU_MODE_IRQ2_MCU_MODE, substream->runtime->rate);
	mt_afe_set_irq_state(Soc_Aud_IRQ_MCU_MODE_IRQ2_MCU_MODE, true);
#ifdef BTDAI_MODEM_INTF
	mt_afe_set_sample_rate(Soc_Aud_Digital_Block_MEM_MOD_DAI, substream->runtime->rate);
	mt_afe_enable_memory_path(Soc_Aud_Digital_Block_MEM_MOD_DAI);
	/* here to turn off digital part */
	mt_afe_set_connection(Soc_Aud_InterCon_Connection, Soc_Aud_InterConnectionInput_I09,
		Soc_Aud_InterConnectionOutput_O12);

	if (mt_afe_get_memory_path_state(Soc_Aud_Digital_Block_MODEM_PCM_2_O) == false) {
		mt_afe_enable_memory_path(Soc_Aud_Digital_Block_MODEM_PCM_2_O);
		extmd_pcm_rx.mPcmModeWidebandSel = (runtime->rate == 8000) ?
			Soc_Aud_PCM_MODE_PCM_MODE_8K : Soc_Aud_PCM_MODE_PCM_MODE_16K;
		SetModemPcmConfig(MODEM_EXTERNAL, extmd_pcm_rx);
		SetModemPcmEnable(MODEM_EXTERNAL, true);
	} else {
		mt_afe_enable_memory_path(Soc_Aud_Digital_Block_MODEM_PCM_2_O);
	}
#else
	mt_afe_set_sample_rate(Soc_Aud_Digital_Block_MEM_DAI, substream->runtime->rate);
	mt_afe_enable_memory_path(Soc_Aud_Digital_Block_MEM_DAI);

	/* here to turn off digital part */
	mt_afe_set_connection(Soc_Aud_InterCon_Connection, Soc_Aud_InterConnectionInput_I02,
		      Soc_Aud_InterConnectionOutput_O11);

	if (mt_afe_get_memory_path_state(Soc_Aud_Digital_Block_DAI_BT) == false) {
		mt_afe_enable_memory_path(Soc_Aud_Digital_Block_DAI_BT);
	/*	SetVoipDAIBTAttribute(substream->runtime->rate);
		SetDaiBtEnable(true);*/
	} else {
		mt_afe_enable_memory_path(Soc_Aud_Digital_Block_DAI_BT);
	}
#endif

	mt_afe_enable_afe(true);
}

static int mtk_bt_dai_pcm_prepare(struct snd_pcm_substream *substream)
{
	pr_debug("mtk_bt_dai_pcm_prepare substream->rate = %d  substream->channels = %d\n",
	       substream->runtime->rate, substream->runtime->channels);
	return 0;
}

static int mtk_bt_dai_alsa_stop(struct snd_pcm_substream *substream)
{
	/* AFE_BLOCK_T *Dai_Block = &(Bt_Dai_Control_context->rBlock); */
	pr_debug("mtk_bt_dai_alsa_stop\n");
#ifdef BTDAI_MODEM_INTF
	mt_afe_disable_memory_path(Soc_Aud_Digital_Block_MEM_MOD_DAI);
	mt_afe_disable_memory_path(Soc_Aud_Digital_Block_MODEM_PCM_2_O);

	if (mt_afe_get_memory_path_state(Soc_Aud_Digital_Block_MODEM_PCM_2_O) == false)
		SetModemPcmEnable(MODEM_EXTERNAL, false);

	StopAudioBtDaiHardware(substream);
	RemoveMemifSubStream(Soc_Aud_Digital_Block_MEM_MOD_DAI, substream);
#else
	mt_afe_disable_memory_path(Soc_Aud_Digital_Block_MEM_DAI);

	mt_afe_disable_memory_path(Soc_Aud_Digital_Block_DAI_BT);

	if (mt_afe_get_memory_path_state(Soc_Aud_Digital_Block_DAI_BT) == false)
		SetDaiBtEnable(false);

	StopAudioBtDaiHardware(substream);
	RemoveMemifSubStream(Soc_Aud_Digital_Block_MEM_DAI, substream);
#endif
	return 0;
}

static int32_t Previous_Hw_cur;
static snd_pcm_uframes_t mtk_bt_dai_pcm_pointer(struct snd_pcm_substream *substream)
{
	int32_t HW_memory_index = 0;
	int32_t HW_Cur_ReadIdx = 0;
	struct AFE_BLOCK_T *Dai_Block = &(Bt_Dai_Control_context->rBlock);
	uint32_t Frameidx = 0;
        struct snd_pcm_runtime *runtime = substream->runtime;
	PRINTK_AUD_DAI("mtk_bt_dai_pcm_pointer Dai_Block->u4DMAReadIdx;= 0x%x\n",
		       Dai_Block->u4WriteIdx);
	/* get total bytes to copy */
	Frameidx = bytes_to_frames(runtime, Dai_Block->u4WriteIdx);
	return Frameidx;

	if (Bt_Dai_Control_context->interruptTrigger == 1) {
		/* get total bytes to copy */
		Frameidx = bytes_to_frames(runtime, Dai_Block->u4DMAReadIdx);
		return Frameidx;
#ifdef BTDAI_MODEM_INTF
		HW_Cur_ReadIdx = align64bytesize(mt_afe_get_reg(AFE_MOD_DAI_CUR));
#else
		HW_Cur_ReadIdx = align64bytesize(mt_afe_get_reg(AFE_DAI_CUR));
#endif
		if (HW_Cur_ReadIdx == 0) {
			pr_debug("[Auddrv] mtk_bt_dai_pcm_pointer  HW_Cur_ReadIdx == 0\n");
			HW_Cur_ReadIdx = Dai_Block->pucPhysBufAddr;
		}
		HW_memory_index = (HW_Cur_ReadIdx - Dai_Block->pucPhysBufAddr);
		Previous_Hw_cur = HW_memory_index;
		pr_debug("[Auddrv] mtk_bt_dai_pcm_pointer =0x%x HW_memory_index = 0x%x\n",
		       HW_Cur_ReadIdx, HW_memory_index);
		Bt_Dai_Control_context->interruptTrigger = 0;
		return HW_memory_index / substream->runtime->channels;
	}
	return Previous_Hw_cur / substream->runtime->channels;
}


static void SetDAIBuffer(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *hw_params)
{
	struct AFE_BLOCK_T *pblock = &Bt_Dai_Control_context->rBlock;
	struct snd_pcm_runtime *runtime = substream->runtime;

	PRINTK_AUD_DAI("SetDAIBuffer\n");
	pblock->pucPhysBufAddr = runtime->dma_addr;
	pblock->pucVirtBufAddr = runtime->dma_area;
	pblock->u4BufferSize = runtime->dma_bytes;
	pblock->u4SampleNumMask = 0x001f;	/* 32 byte align */
	pblock->u4WriteIdx = 0;
	pblock->u4DMAReadIdx = 0;
	pblock->u4DataRemained = 0;
	pblock->u4fsyncflag = false;
	pblock->uResetFlag = true;
	pr_debug("dma_bytes = %d dma_area = %p dma_addr = 0x%x\n",
	       pblock->u4BufferSize, pblock->pucVirtBufAddr, pblock->pucPhysBufAddr);
	/* set sram address top hardware */
#ifdef BTDAI_MODEM_INTF
	mt_afe_set_reg(AFE_MOD_DAI_BASE, pblock->pucPhysBufAddr, 0xffffffff);
	mt_afe_set_reg(AFE_MOD_DAI_END, pblock->pucPhysBufAddr + (pblock->u4BufferSize - 1), 0xffffffff);
#else
	mt_afe_set_reg(AFE_DAI_BASE, pblock->pucPhysBufAddr, 0xffffffff);
	mt_afe_set_reg(AFE_DAI_END, pblock->pucPhysBufAddr + (pblock->u4BufferSize - 1), 0xffffffff);
#endif
}

static int mtk_bt_dai_pcm_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *hw_params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_dma_buffer *dma_buf = &substream->dma_buffer;
	int ret = 0;

	pr_debug("mtk_bt_dai_pcm_hw_params\n");

	dma_buf->dev.type = SNDRV_DMA_TYPE_DEV;
	dma_buf->dev.dev = substream->pcm->card->dev;
	dma_buf->private_data = NULL;

	if (Bt_Dai_Capture_dma_buf->area) {
		pr_debug("mtk_bt_dai_pcm_hw_params Bt_Dai_Capture_dma_buf->area\n");
		runtime->dma_bytes = params_buffer_bytes(hw_params);
		runtime->dma_area = Bt_Dai_Capture_dma_buf->area;
		runtime->dma_addr = Bt_Dai_Capture_dma_buf->addr;
	} else {
		pr_debug("mtk_bt_dai_pcm_hw_params snd_pcm_lib_malloc_pages\n");
		ret = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
	}
	pr_debug("mtk_bt_dai_pcm_hw_params dma_bytes = %zu dma_area = %p dma_addr = 0x%lx\n",
	       runtime->dma_bytes, runtime->dma_area, (long)runtime->dma_addr);

	pr_debug("runtime->hw.buffer_bytes_max = %zu\n", runtime->hw.buffer_bytes_max);
	SetDAIBuffer(substream, hw_params);

	pr_debug("dma_bytes = %zu dma_area = %p dma_addr = 0x%lx\n",
	       substream->runtime->dma_bytes, substream->runtime->dma_area,
	       (long)substream->runtime->dma_addr);
	return ret;
}

static int mtk_bt_dai_capture_pcm_hw_free(struct snd_pcm_substream *substream)
{
	pr_debug("mtk_bt_dai_capture_pcm_hw_free\n");
	if (Bt_Dai_Capture_dma_buf->area)
		return 0;
	else
		return snd_pcm_lib_free_pages(substream);
}


static struct snd_pcm_hw_constraint_list bt_dai_constraints_sample_rates = {

	.count = ARRAY_SIZE(soc_voice_supported_sample_rates),
	.list = soc_voice_supported_sample_rates,
};

static int mtk_bt_dai_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret = 0;

	pr_debug("mtk_bt_dai_pcm_open\n");
#ifdef BTDAI_MODEM_INTF
	Bt_Dai_Control_context = get_mem_control_t(Soc_Aud_Digital_Block_MEM_MOD_DAI);
#else
	Bt_Dai_Control_context = get_mem_control_t(Soc_Aud_Digital_Block_MEM_DAI);
#endif
	runtime->hw = mtk_btdai_hardware;
	memcpy((void *)(&(runtime->hw)), (void *)&mtk_btdai_hardware,
	       sizeof(struct snd_pcm_hardware));
	ret = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				       &bt_dai_constraints_sample_rates);
	ret = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		pr_warn("snd_pcm_hw_constraint_integer failed\n");

	mt_afe_main_clk_on();

	/* print for hw pcm information */
	pr_debug("mtk_bt_dai_pcm_open runtime rate = %d channels = %d\n", runtime->rate,
	       runtime->channels);
	runtime->hw.info |= SNDRV_PCM_INFO_INTERLEAVED;
	runtime->hw.info |= SNDRV_PCM_INFO_NONINTERLEAVED;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		pr_debug("SNDRV_PCM_STREAM_CAPTURE\n");
	else
		return -1;

	if (ret < 0) {
		pr_debug("mtk_bt_dai_pcm_close\n");
		mtk_bt_dai_pcm_close(substream);
		return ret;
	}
	pr_debug("mtk_bt_dai_pcm_open return\n");
	return 0;
}

static int mtk_bt_dai_pcm_close(struct snd_pcm_substream *substream)
{
	mt_afe_main_clk_off();
	return 0;
}

static int mtk_bt_dai_alsa_start(struct snd_pcm_substream *substream)
{
	pr_debug("mtk_bt_dai_alsa_start\n");
#ifdef BTDAI_MODEM_INTF
	set_memif_substream(Soc_Aud_Digital_Block_MEM_MOD_DAI, substream);
#else
	set_memif_substream(Soc_Aud_Digital_Block_MEM_DAI, substream);
#endif
	StartAudioBtDaiHardware(substream);
	return 0;
}

static int mtk_bt_dai_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	pr_debug("mtk_bt_dai_pcm_trigger cmd = %d\n", cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		return mtk_bt_dai_alsa_start(substream);
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		return mtk_bt_dai_alsa_stop(substream);
	}
	return -EINVAL;
}

static bool CheckNullPointer(void *pointer)
{
	if (pointer == NULL) {
		pr_debug("CheckNullPointer pointer = NULL\n");
		return true;
	}
	return false;
}

static int mtk_bt_dai_pcm_copy(struct snd_pcm_substream *substream,
			       int channel, snd_pcm_uframes_t pos,
			       void __user *dst, snd_pcm_uframes_t count)
{
	struct AFE_MEM_CONTROL_T *pDAI_MEM_ConTrol = NULL;
	struct AFE_BLOCK_T *Dai_Block = NULL;
	char *Read_Data_Ptr = (char *)dst;
	ssize_t DMA_Read_Ptr = 0, read_size = 0, read_count = 0;
	unsigned long flags;
        struct snd_pcm_runtime *runtime = substream->runtime;
	pr_debug("%s pos = %lu count = %lu\n", __func__, pos, count);

	/* get total bytes to copy */
	count = align64bytesize(frames_to_bytes(runtime, count));

	/* check which memif nned to be write */
	pDAI_MEM_ConTrol = Bt_Dai_Control_context;
	Dai_Block = &(pDAI_MEM_ConTrol->rBlock);

	if (pDAI_MEM_ConTrol == NULL) {
		pr_warn("cannot find MEM control !!!!!!!\n");
		msleep(50);
		return 0;
	}

	if (Dai_Block->u4BufferSize <= 0) {
		msleep(50);
		return 0;
	}

	if (CheckNullPointer((void *)Dai_Block->pucVirtBufAddr)) {
		pr_warn("CheckNullPointer pucVirtBufAddr = %p\n", Dai_Block->pucVirtBufAddr);
		return 0;
	}

	spin_lock_irqsave(&auddrv_BTDaiInCtl_lock, flags);
	if (Dai_Block->u4DataRemained > Dai_Block->u4BufferSize) {
		PRINTK_AUD_DAI("!!! mtk_bt_dai_pcm_copy u4DataRemained=%x > u4BufferSize=%x\n",
		     Dai_Block->u4DataRemained, Dai_Block->u4BufferSize);
		Dai_Block->u4DataRemained = 0;
		Dai_Block->u4DMAReadIdx = Dai_Block->u4WriteIdx;
	}

	if (count > Dai_Block->u4DataRemained)
		read_size = Dai_Block->u4DataRemained;
	else
		read_size = count;

	DMA_Read_Ptr = Dai_Block->u4DMAReadIdx;
	spin_unlock_irqrestore(&auddrv_BTDaiInCtl_lock, flags);

	PRINTK_AUD_DAI
		("%s finish0, read_count:%x, read_size:%x, u4DataRemained:%x, u4DMAReadIdx:%x, u4WriteIdx:%x\n",
		__func__, read_count, read_size, Dai_Block->u4DataRemained, Dai_Block->u4DMAReadIdx,
		Dai_Block->u4WriteIdx);

	if (DMA_Read_Ptr + read_size < Dai_Block->u4BufferSize) {
		if (DMA_Read_Ptr != Dai_Block->u4DMAReadIdx) {
			PRINTK_AUD_DAI
				("%s 1, read_size:%zu, DataRemained:0x%x, DMA_Read_Ptr:%zu, DMAReadIdx:0x%x\n",
				__func__, read_size, Dai_Block->u4DataRemained,
				DMA_Read_Ptr, Dai_Block->u4DMAReadIdx);
		}

		if (copy_to_user((void __user *)Read_Data_Ptr, (Dai_Block->pucVirtBufAddr + DMA_Read_Ptr),
		     read_size)) {
			pr_err("%s Fail 1 copy to user Read_Data_Ptr:%p, pucVirtBufAddr:%p,", __func__,
				Read_Data_Ptr, Dai_Block->pucVirtBufAddr);
			pr_err(" u4DMAReadIdx:0x%x, DMA_Read_Ptr:%zu,read_size:%zu\n",
				Dai_Block->u4DMAReadIdx, DMA_Read_Ptr, read_size);
			return 0;
		}

		read_count += read_size;
		spin_lock(&auddrv_BTDaiInCtl_lock);
		Dai_Block->u4DataRemained -= read_size;
		Dai_Block->u4DMAReadIdx += read_size;
		Dai_Block->u4DMAReadIdx %= Dai_Block->u4BufferSize;
		DMA_Read_Ptr = Dai_Block->u4DMAReadIdx;
		spin_unlock(&auddrv_BTDaiInCtl_lock);

		Read_Data_Ptr += read_size;
		count -= read_size;

		PRINTK_AUD_DAI
			("%s finish1, copy size:%x, u4DMAReadIdx:%x, u4WriteIdx:%x, u4DataRemained:%x\n",
			__func__, read_size, Dai_Block->u4DMAReadIdx,
			Dai_Block->u4WriteIdx, Dai_Block->u4DataRemained);
	} else {
		uint32_t size_1 = Dai_Block->u4BufferSize - DMA_Read_Ptr;
		uint32_t size_2 = read_size - size_1;

		if (DMA_Read_Ptr != Dai_Block->u4DMAReadIdx) {
			PRINTK_AUD_DAI
				("%s 2, read_size1:%x, DataRemained:%x, DMA_Read_Ptr:%zu, DMAReadIdx:%x\n",
				__func__, size_1, Dai_Block->u4DataRemained, DMA_Read_Ptr, Dai_Block->u4DMAReadIdx);
		}
		if (copy_to_user((void __user *)Read_Data_Ptr,
			(Dai_Block->pucVirtBufAddr + DMA_Read_Ptr), size_1)) {
			pr_err("%s Fail 2 copy to user Read_Data_Ptr:%p, pucVirtBufAddr:%p,", __func__,
				Read_Data_Ptr, Dai_Block->pucVirtBufAddr);
			pr_err(" u4DMAReadIdx:0x%x, DMA_Read_Ptr:%zu,read_size:%zu\n",
				Dai_Block->u4DMAReadIdx, DMA_Read_Ptr, read_size);
			return 0;
		}

		read_count += size_1;
		spin_lock(&auddrv_BTDaiInCtl_lock);
		Dai_Block->u4DataRemained -= size_1;
		Dai_Block->u4DMAReadIdx += size_1;
		Dai_Block->u4DMAReadIdx %= Dai_Block->u4BufferSize;
		DMA_Read_Ptr = Dai_Block->u4DMAReadIdx;
		spin_unlock(&auddrv_BTDaiInCtl_lock);

		PRINTK_AUD_DAI
			("%s finish2, copy size_1:%x, u4DMAReadIdx:%x, u4WriteIdx:%x, u4DataRemained:%x\n",
			__func__, size_1, Dai_Block->u4DMAReadIdx, Dai_Block->u4WriteIdx,
			Dai_Block->u4DataRemained);

		if (DMA_Read_Ptr != Dai_Block->u4DMAReadIdx) {
			PRINTK_AUD_DAI
				("%s 3, read_size2:%x, DataRemained:%x, DMA_Read_Ptr:%zu, DMAReadIdx:%x\n",
				__func__, size_2, Dai_Block->u4DataRemained, DMA_Read_Ptr,
				Dai_Block->u4DMAReadIdx);
		}
		if (copy_to_user((void __user *)(Read_Data_Ptr + size_1),
		     (Dai_Block->pucVirtBufAddr + DMA_Read_Ptr), size_2)) {
			pr_err("%s Fail 3 copy to user Read_Data_Ptr:%p, pucVirtBufAddr:%p,", __func__,
				Read_Data_Ptr, Dai_Block->pucVirtBufAddr);
			pr_err(" u4DMAReadIdx:0x%x, DMA_Read_Ptr:%zu,read_size:%zu\n",
				Dai_Block->u4DMAReadIdx, DMA_Read_Ptr, read_size);
			return read_count << 2;
		}

		read_count += size_2;
		spin_lock(&auddrv_BTDaiInCtl_lock);
		Dai_Block->u4DataRemained -= size_2;
		Dai_Block->u4DMAReadIdx += size_2;
		DMA_Read_Ptr = Dai_Block->u4DMAReadIdx;
		spin_unlock(&auddrv_BTDaiInCtl_lock);

		count -= read_size;
		Read_Data_Ptr += read_size;

		PRINTK_AUD_DAI
			("%s finish3, copy size_2:%x, u4DMAReadIdx:%x, u4WriteIdx:%x u4DataRemained:%x\n",
			__func__, size_2, Dai_Block->u4DMAReadIdx, Dai_Block->u4WriteIdx,
			Dai_Block->u4DataRemained);
	}

	return bytes_to_frames(runtime, count);
}

static int mtk_bt_dai_capture_pcm_silence(struct snd_pcm_substream *substream,
					  int channel, snd_pcm_uframes_t pos,
					  snd_pcm_uframes_t count)
{
	PRINTK_AUD_DAI("dummy_pcm_silence\n");
	return 0;		/* do nothing */
}


static void *dummy_page[2];

static struct page *mtk_bt_dai_capture_pcm_page(struct snd_pcm_substream *substream,
						unsigned long offset)
{
	PRINTK_AUD_DAI("dummy_pcm_page\n");
	return virt_to_page(dummy_page[substream->stream]);	/* the same page */
}


static struct snd_pcm_ops mtk_bt_dai_ops = {

	.open = mtk_bt_dai_pcm_open,
	.close = mtk_bt_dai_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = mtk_bt_dai_pcm_hw_params,
	.hw_free = mtk_bt_dai_capture_pcm_hw_free,
	.prepare = mtk_bt_dai_pcm_prepare,
	.trigger = mtk_bt_dai_pcm_trigger,
	.pointer = mtk_bt_dai_pcm_pointer,
	.copy = mtk_bt_dai_pcm_copy,
	.silence = mtk_bt_dai_capture_pcm_silence,
	.page = mtk_bt_dai_capture_pcm_page,
};

static struct snd_soc_platform_driver mtk_bt_dai_soc_platform = {

	.ops = &mtk_bt_dai_ops,
	.pcm_new = mtk_asoc_bt_dai_pcm_new,
	.probe = mtk_asoc_bt_dai_probe,
};

static int mtk_bt_dai_probe(struct platform_device *pdev)
{
	PRINTK_AUD_DAI("mtk_bt_dai_probe\n");

	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

	if (pdev->dev.of_node)
		dev_set_name(&pdev->dev, "%s", MT_SOC_VOIP_BT_IN);

	pr_debug("%s dev name %s\n", __func__, dev_name(&pdev->dev));
	return snd_soc_register_platform(&pdev->dev, &mtk_bt_dai_soc_platform);
}

static int mtk_asoc_bt_dai_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	PRINTK_AUD_DAI("mtk_asoc_bt_dai_pcm_new\n");
	return 0;
}

static int mtk_asoc_bt_dai_probe(struct snd_soc_platform *platform)
{
	PRINTK_AUD_DAI("mtk_asoc_bt_dai_probe\n");
#ifdef BTDAI_MODEM_INTF
	afe_allocate_mem_buffer(platform->dev, Soc_Aud_Digital_Block_MEM_MOD_DAI,
				   BT_DAI_MAX_BUFFER_SIZE);
	Bt_Dai_Capture_dma_buf = afe_get_mem_buffer(Soc_Aud_Digital_Block_MEM_MOD_DAI);
#else
	afe_allocate_mem_buffer(platform->dev, Soc_Aud_Digital_Block_MEM_DAI,
				   BT_DAI_MAX_BUFFER_SIZE);
	Bt_Dai_Capture_dma_buf = afe_get_mem_buffer(Soc_Aud_Digital_Block_MEM_DAI);
#endif
	return 0;
}

static int mtk_bt_dai_remove(struct platform_device *pdev)
{
	PRINTK_AUD_DAI("%s\n", __func__);
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id mt_soc_pcm_bt_dai_of_ids[] = {

	{.compatible = "mediatek," MT_SOC_VOIP_BT_IN,},
	{}
};

MODULE_DEVICE_TABLE(of, mt_soc_pcm_bt_dai_of_ids);
#endif

static struct platform_driver mtk_bt_dai_capture_driver = {

	.driver = {
		   .name = MT_SOC_VOIP_BT_IN,
		   .owner = THIS_MODULE,
#ifdef CONFIG_OF
		   .of_match_table = mt_soc_pcm_bt_dai_of_ids,
#endif
		   },
	.probe = mtk_bt_dai_probe,
	.remove = mtk_bt_dai_remove,
};

#ifdef CONFIG_OF
module_platform_driver(mtk_bt_dai_capture_driver);
#else
static int __init mtk_soc_bt_dai_platform_init(void)
{
	int ret = 0;

	pr_debug("%s\n", __func__);
	ret = platform_driver_register(&mtk_bt_dai_capture_driver);
	return ret;
}

static void __exit mtk_soc_bt_dai_platform_exit(void)
{
	pr_debug("%s\n", __func__);
	platform_driver_unregister(&mtk_bt_dai_capture_driver);
}
module_init(mtk_soc_bt_dai_platform_init);
module_exit(mtk_soc_bt_dai_platform_exit);
#endif
MODULE_DESCRIPTION("BT DAI module platform driver");
MODULE_LICENSE("GPL");
