/*
 * evt_mask.h
 *
 *  Created on: Apr 12, 2013
 *      Author: g.kruglov
 */

#ifndef EVT_MASK_H_
#define EVT_MASK_H_

// Event masks
#define EVTMSK_RADIO_TX         EVENT_MASK(0)
#define EVTMSK_RADIO_RX         EVENT_MASK(1)
#define EVTMSK_KEYS             EVENT_MASK(2)
#define EVTMSK_IR_RX            EVENT_MASK(3)
#define EVTMSK_TIMER            EVENT_MASK(4)
#define EVTMSK_NEWSECOND        EVENT_MASK(5)
#define EVTMSK_PILL_CHECK       EVENT_MASK(6)
#define EVTMSK_UART_RX_POLL     EVENT_MASK(7)
#define EVTMSK_DFU_REQUEST      EVENT_MASK(8)

#define EVTMSK_USB_READY        EVENT_MASK(10)
#define EVTMSK_NEW_POWER_STATE  EVENT_MASK(11)
#define EVTMSK_LOGFILE          EVENT_MASK(12)

#define EVTMSK_SENS_TABLE_READY EVENT_MASK(20)

#define EVTMSK_MESH_TX          EVENT_MASK(21)
#define EVTMSK_MESH_RX          EVENT_MASK(22)
#define EVTMSK_MESH_NEW_CYC     EVENT_MASK(23)
#define EVTMSK_MESH_RX_END      EVENT_MASK(24)
#define EVTMSK_MESH_PREPARE     EVENT_MASK(25)
#define EVTMSK_MESH_PKT_ENDS    EVENT_MASK(26)

#define EVTMASK_PLAY_ENDS       EVENT_MASK(31)
#define EVTMASK_RADIO           EVENT_MASK(32)

#endif /* EVT_MASK_H_ */
