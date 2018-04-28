#include "platform.h"
#include "pd_phy.h"


static uint64_t rx_edge_ts[PD_RX_TRANSITION_COUNT];
static int rx_edge_ts_idx;

void pd_select_cc(uint8_t cc) {
	GPIO_InitTypeDef GPIO_InitStruct;
	if (cc == PD_CC_1) {	// CC1 is CC, pull-up CC2
		PD_CC_GPIO->ODR |= PD_CC2_PIN;
		GPIO_InitStruct.Pin = PD_CC2_PIN;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_NOPULL;
		HAL_GPIO_Init(PD_CC_GPIO, &GPIO_InitStruct);
		PD_CC_GPIO->ODR |= PD_CC2_PIN;
	} else if (cc == PD_CC_2) {	// CC2 is CC, pull-up CC1
		PD_CC_GPIO->ODR |= PD_CC1_PIN;
		GPIO_InitStruct.Pin = PD_CC1_PIN ;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_NOPULL;
		HAL_GPIO_Init(PD_CC_GPIO, &GPIO_InitStruct);
		PD_CC_GPIO->ODR |= PD_CC1_PIN;
	} else {
		PD_CC_GPIO->ODR &= ~(PD_CC1_PIN | PD_CC2_PIN);
		GPIO_InitStruct.Pin = PD_CC1_PIN | PD_CC2_PIN;
		GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
		GPIO_InitStruct.Pull = GPIO_NOPULL;
		HAL_GPIO_Init(PD_CC_GPIO, &GPIO_InitStruct);
	}
}

void pd_init(void) {
	GPIO_InitTypeDef GPIO_InitStruct;

	/* GPIO Ports Clock Enable */
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	pd_select_cc(0);
	GPIO_InitStruct.Pin = GPIO_PIN_11;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
	GPIOB->ODR |= GPIO_PIN_11;

	// Comparator GPIO
	GPIO_InitStruct.Pin = PD_CC_COMP;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(PD_CC_GPIO, &GPIO_InitStruct);
	pd_rx_disable_monitoring();

	/* EXTI interrupt init*/
	HAL_NVIC_SetPriority(EXTI4_15_IRQn, 0, 0);
	HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
}

void pd_rx_enable_monitoring() {
	/* clear comparator external interrupt */
	EXTI->PR = PD_CC_COMP;		// Pending register
	/* enable comparator external interrupt */
	EXTI->IMR |= PD_CC_COMP;
}

void pd_rx_disable_monitoring() {
	/* disable comparator external interrupt */
	EXTI->IMR &= ~ PD_CC_COMP;
	/* clear comparator external interrupt */
	EXTI->PR = PD_CC_COMP;
}

void pd_rx_start() {
	GPIOB->ODR &= ~GPIO_PIN_11;
}

void pd_rx_handler(void) {
	int next_idx;

	// See RM0091 page 214 & EXTI_PR
	if(__HAL_GPIO_EXTI_GET_IT(PD_CC_COMP) != RESET) {
		rx_edge_ts[rx_edge_ts_idx] = timestamp_get();
		next_idx = (rx_edge_ts_idx == PD_RX_TRANSITION_COUNT - 1) ?
					0 : rx_edge_ts_idx + 1;

		/*
		 * If we have seen enough edges in a certain amount of
		 * time, then trigger RX start.
		 */
		if ((rx_edge_ts[rx_edge_ts_idx] -
		     rx_edge_ts[next_idx])
		     < PD_RX_TRANSITION_WINDOW) {
			/*
			 * ignore the comparator IRQ until we are done
			 * with current message
			 */
			pd_rx_disable_monitoring();

			/* start sampling */
			pd_rx_start();

			/* trigger the analysis in the task */
			return; // pd_rx_event(i);
		} else {
			/* do not trigger RX start, just clear int */
			__HAL_GPIO_EXTI_CLEAR_IT(PD_CC_COMP);
		}
		rx_edge_ts_idx = next_idx;
	}
}
