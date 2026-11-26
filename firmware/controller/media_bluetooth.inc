/* Included by the media BLE driver after ble_start. Main loop only.
 * Stop/resume BLE events without touching shared clocks or recalibrating RF.
 * A local OFF drops a connection; the phone will observe link loss. */
void gp5_bt_set_enabled(int on)
{
    unsigned irq;
    if(!frost_ble_ready || (!!on)==(ble.state!=BLE_OFF))return;
    irq=gp5_irq_global_state();gp5_irq_global(0);
    BIT_SET(BLE+0x00,1u<<8,0);
    R32(BLE+0x0C)=0;R32(BT+0x0C)=0;
    clear_entry();ev.pending=0;
    R32(BLE+0x18)=0xFFFFFFFFu;R32(BT+0x18)=0xFFFFFFFFu;
    if(ble.state==BLE_CONNECTED){++ble.disconnects;ble.last_reason=0x16;}
    ble.state=BLE_OFF;
    tx_reset();portq_head=portq_n=0;sxn=in_sx=0;
    cccd[0]=cccd[1]=0;ble.midi_notify=0;
    if(on){
        core_setup();adv_setup();cn.rxi=0;
        BIT_SET(BLE+0x00,1u<<8,1u<<8);
        ble.state=BLE_ADVERTISING;adv_schedule(slot_now());
    }
    if(irq)gp5_irq_global(1);
}
