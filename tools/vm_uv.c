/* vm_uv.c - the VR menu's touch points for a headless driver (scripts/menu19-drive.sh): for the preload's avail (the
 * layer logs it: `xr: vrmenu: somvr_vrmenu found ... avail 0x...`) every item's parts with their page and the uv of
 * their middle (a row: its label), one per line, tab-separated: label, part (row|less|more|button), page
 * (settings|fight|practice|any), u, v. Build: gcc -std=c11 -D_GNU_SOURCE -I mod/somvr tools/vm_uv.c
 * mod/somvr/somvr_vrmenu.c mod/somvr/somvr_font.c -lm. Usage: vm_uv AVAIL */
#include "somvr_vrmenu.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 2) return fprintf(stderr, "usage: vm_uv AVAIL\n"), 2;
    uint64_t av = strtoull(argv[1], NULL, 0);
    static const char *const part_w[] = {"none", "row", "less", "more", "button"};
    for (int item = 0; item < SOMVR_VMI_ALL; item++)
        for (int part = SOMVR_VMP_ROW; part <= SOMVR_VMP_BUTTON; part++) {
            int rc[4];
            if (!somvr_vm_rect_av(av, item, part, rc)) continue;
            int rp = somvr_vm_row_page(item);
            const char *pg = item == SOMVR_VMI_PG_FIGHT  ? "settings"
                             : item == SOMVR_VMI_PG_BACK ? "fight"
                             : item >= SOMVR_VMI_PR_GO && somvr_vm_practice_action(item) ? "practice"
                             : item == SOMVR_VMI_PR_ORC                                  ? "practice"
                             : rp == SOMVR_VMPG_FIGHT                                    ? "fight"
                             : rp == SOMVR_VMPG_SETTINGS                                 ? "settings"
                                                                                         : "any";
            float x = part == SOMVR_VMP_ROW ? (float)rc[0] + 20 : (float)rc[0] + 0.5f * (float)rc[2];
            printf("%s\t%s\t%s\t%.3f\t%.3f\n", somvr_vm_label(item), part_w[part], pg, x / SOMVR_VM_TEX_W,
                   ((float)rc[1] + 0.5f * (float)rc[3]) / SOMVR_VM_TEX_H);
        }
    return 0;
}
