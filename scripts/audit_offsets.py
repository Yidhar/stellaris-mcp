import pefile

exe_path = r'E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe'
pe = pefile.PE(exe_path, fast_load=True)

def rva_to_offset(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + s.Misc_VirtualSize:
            return rva - s.VirtualAddress + s.PointerToRawData
    return None

def inspect_rva(f, rva, before=16, after=32):
    off = rva_to_offset(rva)
    if off is None:
        return 'Invalid RVA'
    f.seek(max(0, off - before))
    data = f.read(before + after)
    b_part = data[:before].hex(' ')
    a_part = data[before:before+16].hex(' ')
    rest = data[before+16:].hex(' ')
    return f"[-{before}]: {b_part} | [RVA]: {a_part} | [+{16}]: {rest}"

funcs = [
    ('EngineAlloc', 0x20208C8),
    ('PostCommand_45', 0x5F8640),
    ('PostCommand_44', 0x5F8590),
    ('Localize', 0x16D2D0),
    ('FreePdxStr', 0x15BBE0),
    ('PdxStringAssign', 0x15BA40),
    ('alert_OnAlertClick', 0x9E31E0),
    ('notif_NotifLeftClick', 0x3211A0),
    ('event_SelectOption', 0x107AD50),
    ('event_StartScreenDismiss', 0x12D1E50),
    ('event_AnomalyDismiss', 0x11AFAD0),
    ('event_AnomalyResearch', 0xFBA080),
    ('event_FirstContactDismiss', 0x1139FD0),
    ('tech_cancel_execute', 0x6BB250),
    ('tech_scalar_dtor', 0x1AF230),
    ('leader_get_localized_name', 0x3E8E20),
    ('species_copy_ctor', 0x3DE770),
    ('species_dtor', 0x1F8760),
    ('species_trait_set', 0x3D8E50),
    ('ship_register_design', 0x266670),
    ('ship_country_add_design', 0x68A090),
    ('ship_calc_long_name', 0xE0FF10),
    ('ship_can_be_built_by', 0x3B9A10),
    ('ship_set_comp_on_slot', 0xD6E320),
    ('ship_stage_update_res', 0xD6C4A0),
    # Outliner manager functions
    ('outliner_construct_cmd', 0xACDB30),
    ('outliner_construct_bldg', 0xAB5C30),
    ('outliner_enqueue_cmd', 0xB7DB80),
]

with open(exe_path, 'rb') as f:
    for name, rva in funcs:
        print(f"=== {name} (0x{rva:X}) ===")
        print(inspect_rva(f, rva, 8, 32))
