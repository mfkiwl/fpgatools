//
// Author: Wolfgang Spraul
//
// This is free and unencumbered software released into the public domain.
// For details see the UNLICENSE file at the root of the source tree.
//

#include <time.h>

#include "model.h"
#include "floorplan.h"
#include "control.h"

time_t g_start_time;
#define TIME()		(time(0)-g_start_time)
#define TIMESTAMP()	printf("O #NODIFF timestamp %lld\n", (long long) TIME())
#define MEMUSAGE()	printf("O #NODIFF memusage %i\n", get_vm_mb());
#define TIME_AND_MEM()	TIMESTAMP(); MEMUSAGE()

#define AUTOTEST_TMP_DIR	"test.out"

struct test_state
{
	int cmdline_skip;
	char cmdline_diff_exec[1024];
	int dry_run;
	int diff_to_null;

	struct fpga_model* model;
	// test filenames are: tmp_dir/autotest_<base_name>_<diff_counter>.???
	char tmp_dir[256];
	char base_name[256];
	int next_diff_counter;
};

static int dump_file(const char* path)
{
	char line[1024];
	FILE* f;

	printf("\n");
	printf("O begin dump %s\n", path);
	if (!(f = fopen(path, "r")))
		printf("#E error opening %s\n", path);
	else {
		while (fgets(line, sizeof(line), f)) {
			if (!strncmp(line, "--- ", 4)
			    || !strncmp(line, "+++ ", 4)
			    || !strncmp(line, "@@ ", 3))
				continue;
			printf(line);
		}
		fclose(f);
	}
	printf("O end dump %s\n", path);
	return 0;
}

static int diff_start(struct test_state* tstate, const char* base_name)
{
	strcpy(tstate->base_name, base_name);
	tstate->next_diff_counter = 1;
	return 0;
}

static int diff_printf(struct test_state* tstate)
{
	char path[1024], tmp[1024], prior_fp[1024];
	int path_base;
	FILE* dest_f = 0;
	int rc;

	if (tstate->dry_run) {
		printf("O Dry run, skipping diff %i.\n", tstate->next_diff_counter++);
		return 0;
	}
	if (tstate->cmdline_skip >= tstate->next_diff_counter) {
		printf("O Skipping diff %i.\n", tstate->next_diff_counter++);
		return 0;
	}

	snprintf(path, sizeof(path), "%s/autotest_%s_%06i", tstate->tmp_dir,
		tstate->base_name, tstate->next_diff_counter);
	path_base = strlen(path);
	if (tstate->diff_to_null
	    || tstate->next_diff_counter == tstate->cmdline_skip + 1)
		strcpy(prior_fp, "/dev/null");
	else {
		snprintf(prior_fp, sizeof(prior_fp), "%s/autotest_%s_%06i.fp",
			tstate->tmp_dir, tstate->base_name,
			tstate->next_diff_counter-1);
	}

	strcpy(&path[path_base], ".fp");
	dest_f = fopen(path, "w");
	if (!dest_f) FAIL(errno);

	rc = printf_devices(dest_f, tstate->model, /*config_only*/ 1);
	if (rc) FAIL(rc);
	rc = printf_nets(dest_f, tstate->model);
	if (rc) FAIL(rc);

	fclose(dest_f);
	dest_f = 0;
	path[path_base] = 0;
	snprintf(tmp, sizeof(tmp), "%s %s %s.fp >%s.log 2>&1",
		tstate->cmdline_diff_exec, prior_fp, path, path);
	rc = system(tmp);
	if (rc) {
		printf("#E %s:%i system call '%s' failed with code %i, "
			"check %s.log\n", __FILE__, __LINE__, tmp, rc, path);
		// ENOENT comes back when pressing ctrl-c
		if (rc == ENOENT) EXIT(rc);
// todo: report the error up so we can avoid adding a switch to the block list etc.
	}

	strcpy(&path[path_base], ".diff");
	rc = dump_file(path);
	if (rc) FAIL(rc);

	tstate->next_diff_counter++;
	return 0;
fail:
	if (dest_f) fclose(dest_f);
	return rc;
}

// goal: configure logic devices in all supported variations
static int test_logic_config(struct test_state* tstate)
{
	int idx_enum[] = { DEV_LOG_M_OR_L, DEV_LOG_X };
	int y, x, i, j, k, rc;

        y = 68;
	x = 13;

	for (i = 0; i < sizeof(idx_enum)/sizeof(*idx_enum); i++) {
		for (j = LUT_A; j <= LUT_D; j++) {

			// A1..A6 to A..D
			for (k = '1'; k <= '6'; k++) {
				rc = fdev_logic_a2d_lut(tstate->model, y, x,
					idx_enum[i], j, 6, pf("A%c", k), ZTERM);
				if (rc) FAIL(rc);
				rc = fdev_set_required_pins(tstate->model, y, x,
					DEV_LOGIC, idx_enum[i]);
				if (rc) FAIL(rc);

				if (tstate->dry_run)
					fdev_print_required_pins(tstate->model,
						y, x, DEV_LOGIC, idx_enum[i]);
				rc = diff_printf(tstate);
				if (rc) FAIL(rc);
				fdev_delete(tstate->model, y, x, DEV_LOGIC, idx_enum[i]);
			}

			// A1 to O6 to FF to AQ
			rc = fdev_logic_a2d_lut(tstate->model, y, x,
				idx_enum[i], j, 6, "A1", ZTERM);
			if (rc) FAIL(rc);
			rc = fdev_logic_a2d_ff(tstate->model, y, x, idx_enum[i],
				j, MUX_O6, FF_SRINIT0);
			if (rc) FAIL(rc);
			rc = fdev_logic_sync(tstate->model, y, x, idx_enum[i],
				SYNCATTR_ASYNC);
			if (rc) FAIL(rc);
			rc = fdev_logic_clk(tstate->model, y, x, idx_enum[i],
				CLKINV_B);
			if (rc) FAIL(rc);
			rc = fdev_logic_ce_used(tstate->model, y, x, idx_enum[i]);
			if (rc) FAIL(rc);
			rc = fdev_logic_sr_used(tstate->model, y, x, idx_enum[i]);
			if (rc) FAIL(rc);

			rc = fdev_set_required_pins(tstate->model, y, x,
				DEV_LOGIC, idx_enum[i]);
			if (rc) FAIL(rc);

			if (tstate->dry_run)
				fdev_print_required_pins(tstate->model,
					y, x, DEV_LOGIC, idx_enum[i]);
			rc = diff_printf(tstate);
			if (rc) FAIL(rc);
			fdev_delete(tstate->model, y, x, DEV_LOGIC, idx_enum[i]);
		}
	}
	return 0;
fail:
	return rc;
}

static int test_logic_net(struct test_state* tstate, int logic_y, int logic_x,
	int type_idx, pinw_idx_t port, const struct sw_set* logic_switch_set,
	int routing_y, int routing_x, swidx_t routing_sw1, swidx_t routing_sw2)
{
	net_idx_t net_idx;
	struct sw_set routing_switches;
	int dbg, rc;

	dbg = 0;
	rc = fnet_new(tstate->model, &net_idx);
	if (rc) FAIL(rc);

	// add port
	rc = fnet_add_port(tstate->model, net_idx, logic_y, logic_x,
		DEV_LOGIC, type_idx, port);
	if (rc) FAIL(rc);

	// add (one) switch in logic tile
	rc = fnet_add_sw(tstate->model, net_idx,
		logic_y, logic_x, logic_switch_set->sw, logic_switch_set->len);
	if (rc) FAIL(rc);

	// add switches in routing tile
	routing_switches.len = 0;
	if (routing_sw1 == NO_SWITCH) FAIL(EINVAL);
	routing_switches.sw[routing_switches.len++] = routing_sw1;
	if (routing_sw2 != NO_SWITCH)
		routing_switches.sw[routing_switches.len++] = routing_sw2;
	rc = fnet_add_sw(tstate->model, net_idx,
		routing_y, routing_x, routing_switches.sw, routing_switches.len);
	if (rc) FAIL(rc);

	if (dbg)
		printf("lnet %s %s\n",
			routing_sw2 == NO_SWITCH ? "" : fpga_switch_print(
				tstate->model, routing_y, routing_x,
				routing_sw2),
			fpga_switch_print(tstate->model,
				routing_y, routing_x, routing_sw1));

	rc = diff_printf(tstate);
	if (rc) FAIL(rc);

	fnet_delete(tstate->model, net_idx);
	return 0;
fail:
	return rc;
}

static int test_logic_net_l2(struct test_state* tstate, int y, int x,
	int type, int type_idx, str16_t* done_pinw_list, int* done_pinw_len,
	swidx_t* l2_done_list, int* l2_done_len)
{
	struct fpga_device* dev;
	struct switch_to_yx switch_to;
	int i, j, k, l, m, from_to, rc;
	struct sw_set set_l1, set_l2;
	struct fpga_tile* switch_tile;
	int dbg = 0;

	rc = fdev_set_required_pins(tstate->model, y, x, type, type_idx);
	if (rc) FAIL(rc);
	if (dbg)
		fdev_print_required_pins(tstate->model, y, x, type, type_idx);

	dev = fdev_p(tstate->model, y, x, type, type_idx);
	if (!dev) FAIL(EINVAL);
	for (i = 0; i < dev->pinw_req_total; i++) {

		// do every pinw only once across all configs
		for (j = 0; j < *done_pinw_len; j++) {
			if (done_pinw_list[j] == dev->pinw[dev->pinw_req_for_cfg[i]])
				break;
		}
		if (j < *done_pinw_len)
			continue;
		done_pinw_list[(*done_pinw_len)++] = dev->pinw[dev->pinw_req_for_cfg[i]];
	
		from_to = (i < dev->pinw_req_in) ? SW_TO : SW_FROM;
		switch_to.yx_req = YX_ROUTING_TILE;
		switch_to.flags = SWTO_YX_DEF;
		switch_to.model = tstate->model;
		switch_to.y = y;
		switch_to.x = x;
		switch_to.start_switch = dev->pinw[dev->pinw_req_for_cfg[i]];
		switch_to.from_to = from_to;
		rc = fpga_switch_to_yx(&switch_to);
		if (rc) FAIL(rc);
		if (dbg)
			printf_switch_to_yx_result(&switch_to);

		switch_tile = YX_TILE(tstate->model, switch_to.dest_y, switch_to.dest_x);
		rc = fpga_swset_fromto(tstate->model, switch_to.dest_y,
			switch_to.dest_x, switch_to.dest_connpt, from_to, &set_l1);
		if (rc) FAIL(rc);
		if (dbg)
			fpga_swset_print(tstate->model, switch_to.dest_y,
				switch_to.dest_x, &set_l1, from_to);

		for (j = 0; j < set_l1.len; j++) {
   			for (k = 0; k < 2; k++) {
				// k == 0 is the SW_FROM round, k == 1 is the SW_TO round.
				// For out-pins, we don't need the SW_TO round because they
				// would create multiple sources driving one pin which is
				// illegal.
				if (k && i >= dev->pinw_req_in)
					break;

				rc = fpga_swset_fromto(tstate->model, switch_to.dest_y,
					switch_to.dest_x, CONNPT_STR16(switch_tile,
					SW_I(switch_tile->switches[set_l1.sw[j]], !from_to)),
					k ? SW_TO : SW_FROM, &set_l2);
				if (rc) FAIL(rc);

				for (l = 0; l < set_l2.len; l++) {

					// duplicate check
					for (m = 0; m < *l2_done_len; m++) {
						if (l2_done_list[m] == set_l2.sw[l])
							break;
					}
					if (m < *l2_done_len)
						continue;
					l2_done_list[(*l2_done_len)++] = set_l2.sw[l];
					if (tstate->dry_run)
						printf("l2_done_list %s at %i\n", fpga_switch_print(tstate->model,
							switch_to.dest_y, switch_to.dest_x, l2_done_list[(*l2_done_len)-1]),
							(*l2_done_len)-1);

					// we did the l1 switches in an earlier round, but have to
					// redo them before every l2 switch to make a clean diff
					// on top of l1. The l2 can be in the same mip as the l1
					// so it has to be repeated for every l2 switch, not just
					// once for the set.
					rc = test_logic_net(tstate, y, x, type_idx, dev->pinw_req_for_cfg[i],
						&switch_to.set, switch_to.dest_y, switch_to.dest_x,
						set_l1.sw[j], NO_SWITCH);
					if (rc) FAIL(rc);

					rc = test_logic_net(tstate, y, x, type_idx, dev->pinw_req_for_cfg[i],
						&switch_to.set, switch_to.dest_y, switch_to.dest_x,
						set_l1.sw[j], set_l2.sw[l]);
					if (rc) FAIL(rc);
				}
			}
		}
	}
	return 0;
fail:
	return rc;
}

static int test_logic_net_l1(struct test_state* tstate, int y, int x,
	int type, int type_idx, str16_t* done_pinw_list, int* done_pinw_len,
	swidx_t* done_sw_list, int* done_sw_len)
{
	struct fpga_device* dev;
	struct switch_to_yx switch_to;
	int i, j, k, from_to, rc;
	struct sw_set set_l1;
	int dbg = 0;

	rc = fdev_set_required_pins(tstate->model, y, x, type, type_idx);
	if (rc) FAIL(rc);
	if (tstate->dry_run)
		fdev_print_required_pins(tstate->model, y, x, type, type_idx);

	dev = fdev_p(tstate->model, y, x, type, type_idx);
	if (!dev) FAIL(EINVAL);
	for (i = 0; i < dev->pinw_req_total; i++) {

		// do every pinw only once across all configs
		for (j = 0; j < *done_pinw_len; j++) {
			if (done_pinw_list[j] == dev->pinw[dev->pinw_req_for_cfg[i]])
				break;
		}
		if (j < *done_pinw_len)
			continue;
		done_pinw_list[(*done_pinw_len)++] = dev->pinw[dev->pinw_req_for_cfg[i]];

		from_to = (i < dev->pinw_req_in) ? SW_TO : SW_FROM;
		switch_to.yx_req = YX_ROUTING_TILE;
		switch_to.flags = SWTO_YX_DEF;
		switch_to.model = tstate->model;
		switch_to.y = y;
		switch_to.x = x;
		switch_to.start_switch = dev->pinw[dev->pinw_req_for_cfg[i]];
		switch_to.from_to = from_to;
		rc = fpga_switch_to_yx(&switch_to);
		if (rc) FAIL(rc);
		if (tstate->dry_run)
			printf_switch_to_yx_result(&switch_to);

		rc = fpga_swset_fromto(tstate->model, switch_to.dest_y,
			switch_to.dest_x, switch_to.dest_connpt, from_to, &set_l1);
		if (rc) FAIL(rc);
		if (dbg)
			fpga_swset_print(tstate->model, switch_to.dest_y,
				switch_to.dest_x, &set_l1, from_to);

		for (j = 0; j < set_l1.len; j++) {
			// an out-pin can go directly to an in-pin and
			// we don't need that pin twice
			for (k = 0; k < *done_sw_len; k++) {
				if (done_sw_list[k] == set_l1.sw[j])
					break;
			}
			if (k < *done_sw_len)
				continue;

			rc = test_logic_net(tstate, y, x, type_idx, dev->pinw_req_for_cfg[i],
				&switch_to.set, switch_to.dest_y, switch_to.dest_x,
				set_l1.sw[j], NO_SWITCH);
			if (rc) FAIL(rc);
			done_sw_list[(*done_sw_len)++] = set_l1.sw[j];
			if (tstate->dry_run)
				printf("done_list %s at %i\n", fpga_switch_print(tstate->model,
					switch_to.dest_y, switch_to.dest_x, set_l1.sw[j]),
					(*done_sw_len)-1);
		}
	}
	return 0;
fail:
	return rc;
}

static int test_switches(struct test_state* tstate, int y, int x,
	str16_t start_switch, net_idx_t net, swidx_t* done_list, int* done_list_len)
{
	struct sw_set sw_set, w4_set;
	const char* switch_str;
	str16_t switch_str_i;
	int i, j, k, rc;

	rc = fpga_swset_fromto(tstate->model, y, x, start_switch, SW_TO, &sw_set);
	if (rc) FAIL(rc);
	if (tstate->dry_run)
		fpga_swset_print(tstate->model, y, x, &sw_set, SW_TO);

	for (i = 0; i < sw_set.len; i++) {
		switch_str_i = fpga_switch_str_i(tstate->model, y, x,
			sw_set.sw[i], SW_FROM);
		switch_str = strarray_lookup(&tstate->model->str, switch_str_i);
		if (!switch_str) FAIL(EINVAL);
		if (switch_str[2] == '4') {
			// base for len-4 wire
			if (tstate->dry_run)
				fnet_printf(stdout, tstate->model, net);
			rc = diff_printf(tstate);
			if (rc) FAIL(rc);

			// add len-4 wire
			rc = fnet_add_sw(tstate->model, net, y, x,
				&sw_set.sw[i], 1);
			if (rc) FAIL(rc);

			// enum dests of len-4 wire
			rc = fpga_swset_fromto(tstate->model, y, x,
				switch_str_i, SW_FROM, &w4_set);
			if (rc) FAIL(rc);
			if (tstate->dry_run)
				fpga_swset_print(tstate->model, y, x,
					&w4_set, SW_FROM);

			for (j = 0; j < w4_set.len; j++) {
				// do not point to our base twice
				if (w4_set.sw[j] == sw_set.sw[i])
					continue;

				// duplicate check and done_list
				for (k = 0; k < *done_list_len; k++) {
					if (done_list[k] == w4_set.sw[j])
						break;
				}
				if (k < *done_list_len)
					continue;
				done_list[(*done_list_len)++] = w4_set.sw[j];
				if (tstate->dry_run)
					printf("done_list %s at %i\n",
						fpga_switch_print(tstate->model,
						  y, x, done_list[(*done_list_len)-1]),
						  (*done_list_len)-1);

				// base for len-4 target
				if (tstate->dry_run)
					fnet_printf(stdout, tstate->model, net);
				rc = diff_printf(tstate);
				if (rc) FAIL(rc);

				// add len-4 target
				rc = fnet_add_sw(tstate->model, net, y, x,
					&w4_set.sw[j], 1);
				if (rc) FAIL(rc);

				if (tstate->dry_run)
					fnet_printf(stdout, tstate->model, net);
				rc = diff_printf(tstate);
				if (rc) FAIL(rc);

				rc = fnet_remove_sw(tstate->model, net, y, x,
					&w4_set.sw[j], 1);
				if (rc) FAIL(rc);
			}
			rc = fnet_remove_sw(tstate->model, net, y, x,
				&sw_set.sw[i], 1);
			if (rc) FAIL(rc);
		}
	}
	return 0;
fail:
	return rc;
}

int test_routing_sw_from_iob(struct test_state* tstate,
	swidx_t* done_list, int* done_list_len);
int test_routing_sw_from_iob(struct test_state* tstate,
	swidx_t* done_list, int* done_list_len)
{
	struct switch_to_yx switch_to;
	int iob_y, iob_x, iob_type_idx, rc;
	struct fpga_device* iob_dev;
	net_idx_t net;

	rc = fpga_find_iob(tstate->model, "P48", &iob_y, &iob_x, &iob_type_idx);
	if (rc) FAIL(rc);
	rc = fdev_iob_output(tstate->model, iob_y, iob_x, iob_type_idx);
	if (rc) FAIL(rc);
	iob_dev = fdev_p(tstate->model, iob_y, iob_x, DEV_IOB, iob_type_idx);
	if (!iob_dev) FAIL(EINVAL);

	rc = fnet_new(tstate->model, &net);
	if (rc) FAIL(rc);
	rc = fnet_add_port(tstate->model, net, iob_y, iob_x,
		DEV_IOB, iob_type_idx, IOB_IN_O);
	if (rc) FAIL(rc);

	switch_to.yx_req = YX_DEV_OLOGIC;
	switch_to.flags = SWTO_YX_DEF;
	switch_to.model = tstate->model;
	switch_to.y = iob_y;
	switch_to.x = iob_x;
	switch_to.start_switch = iob_dev->pinw[IOB_IN_O];
	switch_to.from_to = SW_TO;
	rc = fpga_switch_to_yx(&switch_to);
	if (rc) FAIL(rc);
	if (tstate->dry_run)
		printf_switch_to_yx_result(&switch_to);
	rc = fnet_add_sw(tstate->model, net, switch_to.y,
		switch_to.x, switch_to.set.sw, switch_to.set.len);
	if (rc) FAIL(rc);

	switch_to.yx_req = YX_ROUTING_TILE;
	switch_to.flags = SWTO_YX_DEF;
	switch_to.model = tstate->model;
	switch_to.y = switch_to.dest_y;
	switch_to.x = switch_to.dest_x;
	switch_to.start_switch = switch_to.dest_connpt;
	switch_to.from_to = SW_TO;
	rc = fpga_switch_to_yx(&switch_to);
	if (rc) FAIL(rc);
	rc = fnet_add_sw(tstate->model, net, switch_to.y,
		switch_to.x, switch_to.set.sw, switch_to.set.len);
	if (rc) FAIL(rc);
	if (tstate->dry_run)
		printf_switch_to_yx_result(&switch_to);

	switch_to.yx_req = YX_ROUTING_TO_FABLOGIC;
	switch_to.flags = SWTO_YX_CLOSEST;
	switch_to.model = tstate->model;
	switch_to.y = switch_to.dest_y;
	switch_to.x = switch_to.dest_x;
	switch_to.start_switch = switch_to.dest_connpt;
	switch_to.from_to = SW_TO;
	rc = fpga_switch_to_yx(&switch_to);
	if (rc) FAIL(rc);
	if (tstate->dry_run)
		printf_switch_to_yx_result(&switch_to);
	rc = fnet_add_sw(tstate->model, net, switch_to.y,
		switch_to.x, switch_to.set.sw, switch_to.set.len);
	if (rc) FAIL(rc);

	rc = test_switches(tstate, switch_to.dest_y, switch_to.dest_x,
		switch_to.dest_connpt, net, done_list, done_list_len);
	if (rc) FAIL(rc);

	fdev_delete(tstate->model, iob_y, iob_x, DEV_IOB, iob_type_idx);
	fnet_delete(tstate->model, net);
	return 0;
fail:
	return rc;
}

static int test_routing_sw_from_logic(struct test_state* tstate,
	swidx_t* done_list, int* done_list_len)
{
	struct fpga_device* dev;
	struct switch_to_rel swto;
	struct sw_conns conns;
	const char* str;
	net_idx_t net;
	int y, x, rel_y, i, rc;

        y = 67;
	x = 13;

	// We loop over this twice, once with rel_y==-1 and then with rel_y==+1
	// That way we come into the routing switchbox over the nr1/nl1 wires
	// from below, or sr1/sl1 wires from above.
	for (rel_y = -1; rel_y <= 1; rel_y += 2) {
		for (i = '1'; i <= '6'; i++) {
			rc = fdev_logic_a2d_lut(tstate->model, y, x,
				DEV_LOG_M_OR_L, LUT_A, 6, pf("A%c", i), ZTERM);
			if (rc) FAIL(rc);
			rc = fdev_set_required_pins(tstate->model, y, x,
				DEV_LOGIC, DEV_LOG_M_OR_L);
			if (rc) FAIL(rc);
		
			if (tstate->dry_run)
				fdev_print_required_pins(tstate->model,
					y, x, DEV_LOGIC, DEV_LOG_M_OR_L);
			dev = fdev_p(tstate->model, y, x, DEV_LOGIC, DEV_LOG_M_OR_L);
			if (!dev) FAIL(EINVAL);
			if (!dev->pinw_req_in) FAIL(EINVAL);
		
			rc = fnet_new(tstate->model, &net);
			if (rc) FAIL(rc);
			rc = fnet_add_port(tstate->model, net, y, x,
				DEV_LOGIC, DEV_LOG_M_OR_L, dev->pinw_req_for_cfg[0]);
			if (rc) FAIL(rc);
		
			swto.model = tstate->model;
			swto.start_y = y;
			swto.start_x = x;
			swto.start_switch = fdev_logic_pinstr_i(tstate->model,
				dev->pinw_req_for_cfg[0]|LD1, LOGIC_M);
			swto.from_to = SW_TO;
			swto.rel_y = 0;
			swto.rel_x = -1;
			swto.target_connpt = STRIDX_NO_ENTRY;
			rc = fpga_switch_to_rel(&swto);
			if (rc) FAIL(rc);
			if (!swto.set.len) FAIL(EINVAL);
			if (tstate->dry_run)
				printf_switch_to_rel_result(&swto);
			rc = fnet_add_sw(tstate->model, net, swto.start_y,
				swto.start_x, swto.set.sw, swto.set.len);
			if (rc) FAIL(rc);
		
			rc = construct_sw_conns(&conns, tstate->model, swto.dest_y, swto.dest_x,
				swto.dest_connpt, SW_TO, /*max_depth*/ 1);
			if (rc) FAIL(rc);
				
			while (fpga_switch_conns(&conns) != NO_CONN) {
				if (conns.dest_x != swto.dest_x
				    || conns.dest_y != swto.dest_y+rel_y)
					continue;
				str = strarray_lookup(&tstate->model->str, conns.dest_str_i);
				if (!str) { HERE(); continue; }
				if (strlen(str) < 5
				    || str[2] != '1' || str[3] != 'B')
					continue;
				rc = fnet_add_sw(tstate->model, net, swto.dest_y,
					swto.dest_x, conns.chain.set.sw, conns.chain.set.len);
				if (rc) FAIL(rc);
				if (tstate->dry_run)
					fnet_printf(stdout, tstate->model, net);
		
				rc = test_switches(tstate, conns.dest_y, conns.dest_x,
					conns.dest_str_i, net, done_list, done_list_len);
				if (rc) FAIL(rc);
		
				rc = fnet_remove_sw(tstate->model, net, swto.dest_y,
					swto.dest_x, conns.chain.set.sw, conns.chain.set.len);
				if (rc) FAIL(rc);
			}
			destruct_sw_conns(&conns);
			fdev_delete(tstate->model, y, x, DEV_LOGIC, DEV_LOG_M_OR_L);
			fnet_delete(tstate->model, net);
		}
	}
	return 0;
fail:
	return rc;
}

// goal: use all switches in a routing switchbox
static int test_routing_switches(struct test_state* tstate)
{
	int idx_enum[] = { DEV_LOG_M_OR_L, DEV_LOG_X };
	int y, x, i, j, k, r, rc;
	swidx_t done_sw_list[MAX_SWITCHBOX_SIZE];
	int done_sw_len;
	str16_t done_pinw_list[2000];
	int done_pinw_len;
	
        y = 68;
	x = 13;
	done_sw_len = 0;
	for (r = 0; r <= 1; r++) {
		// two rounds:
		// r == 0: round over all configs with single-level nets only
		// r == 1: second round with two-level nets

		done_pinw_len = 0; // reset done pinwires for each round

		for (i = 0; i < sizeof(idx_enum)/sizeof(*idx_enum); i++) {
			for (j = LUT_A; j <= LUT_D; j++) {
	
				// A1-A6 to A (same for lut B-D)
				for (k = '1'; k <= '6'; k++) {
					rc = fdev_logic_a2d_lut(tstate->model, y, x,
						idx_enum[i], j, 6, pf("A%c", k), ZTERM);
					if (rc) FAIL(rc);
	
					if (!r)
						rc = test_logic_net_l1(tstate, y, x, DEV_LOGIC,
							idx_enum[i], done_pinw_list, &done_pinw_len,
							done_sw_list, &done_sw_len);
					else
						rc = test_logic_net_l2(tstate, y, x, DEV_LOGIC,
							idx_enum[i], done_pinw_list, &done_pinw_len,
							done_sw_list, &done_sw_len);
					if (rc) FAIL(rc);
					fdev_delete(tstate->model, y, x, DEV_LOGIC, idx_enum[i]);
				}
	
				// A1->O6->FF->AQ (same for lut B-D)
				rc = fdev_logic_a2d_lut(tstate->model, y, x,
					idx_enum[i], j, 6, "A1", ZTERM);
				if (rc) FAIL(rc);
				rc = fdev_logic_a2d_ff(tstate->model, y, x, idx_enum[i],
					j, MUX_O6, FF_SRINIT0);
				if (rc) FAIL(rc);
				rc = fdev_logic_sync(tstate->model, y, x, idx_enum[i],
					SYNCATTR_ASYNC);
				if (rc) FAIL(rc);
				rc = fdev_logic_clk(tstate->model, y, x, idx_enum[i],
					CLKINV_B);
				if (rc) FAIL(rc);
				rc = fdev_logic_ce_used(tstate->model, y, x, idx_enum[i]);
				if (rc) FAIL(rc);
				rc = fdev_logic_sr_used(tstate->model, y, x, idx_enum[i]);
				if (rc) FAIL(rc);
	
				rc = fdev_set_required_pins(tstate->model, y, x,
					DEV_LOGIC, idx_enum[i]);
				if (rc) FAIL(rc);
	
				if (!r)
					rc = test_logic_net_l1(tstate, y, x, DEV_LOGIC,
						idx_enum[i], done_pinw_list, &done_pinw_len,
						done_sw_list, &done_sw_len);
				else
					rc = test_logic_net_l2(tstate, y, x, DEV_LOGIC,
						idx_enum[i], done_pinw_list, &done_pinw_len,
						done_sw_list, &done_sw_len);
				if (rc) FAIL(rc);
				fdev_delete(tstate->model, y, x, DEV_LOGIC, idx_enum[i]);
			}
		}
	}
	done_sw_len = 0;
	rc = test_routing_sw_from_logic(tstate, done_sw_list, &done_sw_len);
	if (rc) FAIL(rc);
	return 0;
fail:
	return rc;
}

static int test_iologic_switches2(struct test_state* tstate, int iob_y, int iob_x, int iob_type_idx)
{
	struct fpga_device* iob_dev;
	struct switch_to_yx switch_to;
	struct sw_chain chain;
	net_idx_t net_idx;
	int i, from_to, rc;

	rc = fdev_set_required_pins(tstate->model, iob_y, iob_x, DEV_IOB, iob_type_idx);
	if (rc) FAIL(rc);
	if (tstate->dry_run)
		fdev_print_required_pins(tstate->model, iob_y, iob_x, DEV_IOB, iob_type_idx);
	iob_dev = fdev_p(tstate->model, iob_y, iob_x, DEV_IOB, iob_type_idx);
	if (!iob_dev) FAIL(EINVAL);

	for (i = 0; i < iob_dev->pinw_req_total; i++) {
		from_to = i >= iob_dev->pinw_req_in ? SW_FROM : SW_TO;

		// determine switch in iob to reach iologic tile
		switch_to.yx_req = YX_DEV_ILOGIC;
		switch_to.flags = SWTO_YX_DEF;
		switch_to.model = tstate->model;
		switch_to.y = iob_y;
		switch_to.x = iob_x;
		switch_to.start_switch = iob_dev->pinw[iob_dev->pinw_req_for_cfg[i]];
		switch_to.from_to = from_to;
		rc = fpga_switch_to_yx(&switch_to);
		if (rc) FAIL(rc);
		if (tstate->dry_run)
			printf_switch_to_yx_result(&switch_to);
	
		if (construct_sw_chain(&chain, tstate->model, switch_to.dest_y,
			switch_to.dest_x, switch_to.dest_connpt, from_to,
			/*max_depth*/ -1, /*block_list*/ 0, /*block_list_len*/ 0))
			FAIL(EINVAL);
		while (fpga_switch_chain(&chain) != NO_CONN) {
	
			if (tstate->dry_run)
				printf("sw %s\n", fmt_swset(tstate->model,
					switch_to.dest_y, switch_to.dest_x,
					&chain.set, from_to));
	
			// new net
			rc = fnet_new(tstate->model, &net_idx);
			if (rc) FAIL(rc);
	
			// add iob port
			rc = fnet_add_port(tstate->model, net_idx, iob_y, iob_x,
				DEV_IOB, iob_type_idx, IOB_IN_O);
			if (rc) FAIL(rc);
	
			// add switch in iob tile
			rc = fnet_add_sw(tstate->model, net_idx, switch_to.y,
				switch_to.x, switch_to.set.sw, switch_to.set.len);
			if (rc) FAIL(rc);
	
			// add all but last switch in set
			if (chain.set.len > 1) {
				rc = fnet_add_sw(tstate->model, net_idx, switch_to.dest_y,
					switch_to.dest_x, chain.set.sw, chain.set.len-1);
				if (rc) FAIL(rc);
			}
			rc = diff_printf(tstate);
			if (rc) FAIL(rc);
	
			// add last switch
			rc = fnet_add_sw(tstate->model, net_idx, switch_to.dest_y,
				switch_to.dest_x, &chain.set.sw[chain.set.len-1], 1);
			if (rc) FAIL(rc);
	
			rc = diff_printf(tstate);
			if (rc) FAIL(rc);
	
			fnet_delete(tstate->model, net_idx);
		}
		destruct_sw_chain(&chain);
	}
	return 0;
fail:
	return rc;
}

static int test_iologic_switches(struct test_state* tstate)
{
	char iob_name[32];
	int iob_y, iob_x, iob_type_idx, i, rc;

	for (i = 45; i <= 48; i++) {
		snprintf(iob_name, sizeof(iob_name), "P%i", i);

		// input IOB
		rc = fpga_find_iob(tstate->model, iob_name, &iob_y, &iob_x, &iob_type_idx);
		if (rc) FAIL(rc);
		rc = fdev_iob_input(tstate->model, iob_y, iob_x, iob_type_idx);
		if (rc) FAIL(rc);
		rc = test_iologic_switches2(tstate, iob_y, iob_x, iob_type_idx);
		if (rc) FAIL(rc);
		fdev_delete(tstate->model, iob_y, iob_x, DEV_IOB, iob_type_idx);

		// output IOB
		rc = fpga_find_iob(tstate->model, iob_name, &iob_y, &iob_x, &iob_type_idx);
		if (rc) FAIL(rc);
		rc = fdev_iob_output(tstate->model, iob_y, iob_x, iob_type_idx);
		if (rc) FAIL(rc);
		rc = test_iologic_switches2(tstate, iob_y, iob_x, iob_type_idx);
		if (rc) FAIL(rc);
		fdev_delete(tstate->model, iob_y, iob_x, DEV_IOB, iob_type_idx);
	}
	return 0;
fail:
	return rc;
}

static int test_iob_config(struct test_state* tstate)
{
	int iob_y, iob_x, iob_type_idx, rc;
	net_idx_t net_idx;
	struct fpga_device* dev;

	tstate->diff_to_null = 1;

	// P45 is an IOBS
	rc = fpga_find_iob(tstate->model, "P45", &iob_y, &iob_x, &iob_type_idx);
	if (rc) FAIL(rc);
	rc = fdev_iob_input(tstate->model, iob_y, iob_x, iob_type_idx);
	if (rc) FAIL(rc);
	dev = fdev_p(tstate->model, iob_y, iob_x, DEV_IOB, iob_type_idx);
	if (!dev) FAIL(EINVAL);

	if ((rc = diff_printf(tstate))) FAIL(rc);
	dev->u.iob.I_mux = IMUX_I_B;
	if ((rc = diff_printf(tstate))) FAIL(rc);

	fdev_delete(tstate->model, iob_y, iob_x, DEV_IOB, iob_type_idx);

	// P46 is an IOBM
	rc = fpga_find_iob(tstate->model, "P46", &iob_y, &iob_x, &iob_type_idx);
	if (rc) FAIL(rc);
	rc = fdev_iob_input(tstate->model, iob_y, iob_x, iob_type_idx);
	if (rc) FAIL(rc);
	if ((rc = diff_printf(tstate))) FAIL(rc);
	fdev_delete(tstate->model, iob_y, iob_x, DEV_IOB, iob_type_idx);

	// P47 is an IOBS
	rc = fpga_find_iob(tstate->model, "P47", &iob_y, &iob_x, &iob_type_idx);
	if (rc) FAIL(rc);
	rc = fdev_iob_output(tstate->model, iob_y, iob_x, iob_type_idx);
	if (rc) FAIL(rc);
	dev = fdev_p(tstate->model, iob_y, iob_x, DEV_IOB, iob_type_idx);
	if (!dev) FAIL(EINVAL);

	// least amount of bits:
	dev->u.iob.slew = SLEW_SLOW;
	dev->u.iob.drive_strength = 8;
	dev->u.iob.suspend = SUSP_3STATE;

	dev->u.iob.suspend = SUSP_3STATE;
	rc = diff_printf(tstate); if (rc) FAIL(rc);
	dev->u.iob.suspend = SUSP_3STATE_OCT_ON;
	rc = diff_printf(tstate); if (rc) FAIL(rc);
	dev->u.iob.suspend = SUSP_3STATE_KEEPER;
	rc = diff_printf(tstate); if (rc) FAIL(rc);
	dev->u.iob.suspend = SUSP_3STATE_PULLUP;
	rc = diff_printf(tstate); if (rc) FAIL(rc);
	dev->u.iob.suspend = SUSP_3STATE_PULLDOWN;
	rc = diff_printf(tstate); if (rc) FAIL(rc);
	dev->u.iob.suspend = SUSP_LAST_VAL;
	rc = diff_printf(tstate); if (rc) FAIL(rc);
	dev->u.iob.suspend = SUSP_3STATE;

	dev->u.iob.drive_strength = 2;
	rc = diff_printf(tstate); if (rc) FAIL(rc);
	dev->u.iob.drive_strength = 4;
	rc = diff_printf(tstate); if (rc) FAIL(rc);
	dev->u.iob.drive_strength = 6;
	rc = diff_printf(tstate); if (rc) FAIL(rc);
	dev->u.iob.drive_strength = 8;
	rc = diff_printf(tstate); if (rc) FAIL(rc);
	dev->u.iob.drive_strength = 12;
	rc = diff_printf(tstate); if (rc) FAIL(rc);
	dev->u.iob.drive_strength = 16;
	rc = diff_printf(tstate); if (rc) FAIL(rc);
	dev->u.iob.drive_strength = 24;
	rc = diff_printf(tstate); if (rc) FAIL(rc);
	dev->u.iob.drive_strength = 8;

	dev->u.iob.slew = SLEW_SLOW;
	rc = diff_printf(tstate); if (rc) FAIL(rc);
	dev->u.iob.slew = SLEW_FAST;
	rc = diff_printf(tstate); if (rc) FAIL(rc);
	dev->u.iob.slew = SLEW_QUIETIO;
	rc = diff_printf(tstate); if (rc) FAIL(rc);
	dev->u.iob.slew = SLEW_SLOW;

	fdev_delete(tstate->model, iob_y, iob_x, DEV_IOB, iob_type_idx);

	// P48 is an IOBM
	rc = fpga_find_iob(tstate->model, "P48", &iob_y, &iob_x, &iob_type_idx);
	if (rc) FAIL(rc);
	rc = fdev_iob_output(tstate->model, iob_y, iob_x, iob_type_idx);
	if (rc) FAIL(rc);
	dev = fdev_p(tstate->model, iob_y, iob_x, DEV_IOB, iob_type_idx);
	if (!dev) FAIL(EINVAL);

	// least bits
	dev->u.iob.slew = SLEW_SLOW;
	dev->u.iob.drive_strength = 8;
	dev->u.iob.suspend = SUSP_3STATE;

	// new net
	rc = fnet_new(tstate->model, &net_idx);
	if (rc) FAIL(rc);
	
	// add iob port
	rc = fnet_add_port(tstate->model, net_idx, iob_y, iob_x,
		DEV_IOB, iob_type_idx, IOB_IN_O);
	if (rc) FAIL(rc);
	if ((rc = diff_printf(tstate))) FAIL(rc);

	fdev_delete(tstate->model, iob_y, iob_x, DEV_IOB, iob_type_idx);
	return 0;
fail:
	return rc;
}

static int test_lut(struct test_state* tstate, int y, int x, int type_idx,
	int lut, const char* lut6, const char* lut5)
{
	struct fpga_device* dev;
	net_idx_t pinw_nets[MAX_NUM_PINW];
	int i, rc;

	if (tstate->dry_run)
		printf("O lut6 '%s' lut5 '%s'\n",
			lut6 ? lut6 : "-", lut5 ? lut5 : "-");
	if (lut6) {
		rc = fdev_logic_a2d_lut(tstate->model, y, x,
			type_idx, lut, 6, lut6, ZTERM);
		if (rc) FAIL(rc);
	}
	if (lut5) {
		rc = fdev_logic_a2d_lut(tstate->model, y, x,
			type_idx, lut, 5, lut5, ZTERM);
		if (rc) FAIL(rc);
	}
	rc = fdev_set_required_pins(tstate->model, y, x, DEV_LOGIC, type_idx);
	if (rc) FAIL(rc);
	if (tstate->dry_run) {
		fdev_print_required_pins(tstate->model, y, x,
			DEV_LOGIC, type_idx);
	}

	// add stub nets for each required pin
	dev = fdev_p(tstate->model, y, x, DEV_LOGIC, type_idx);
	if (!dev) FAIL(EINVAL);
	for (i = 0; i < dev->pinw_req_total; i++) {
		// i < dev->pinw_req_in -> input
		rc = fnet_new(tstate->model, &pinw_nets[i]);
		if (rc) FAIL(rc);
		rc = fnet_add_port(tstate->model, pinw_nets[i], y, x,
			DEV_LOGIC, type_idx, dev->pinw_req_for_cfg[i]);
		if (rc) FAIL(rc);
		if ((dev->pinw_req_for_cfg[i] == LI_A6
		     && dev->u.logic.a2d[LUT_A].lut5
		     && *dev->u.logic.a2d[LUT_A].lut5)
		    || (dev->pinw_req_for_cfg[i] == LI_B6
		        && dev->u.logic.a2d[LUT_B].lut5
		        && *dev->u.logic.a2d[LUT_B].lut5)
		    || (dev->pinw_req_for_cfg[i] == LI_C6
			&& dev->u.logic.a2d[LUT_C].lut5
			&& *dev->u.logic.a2d[LUT_C].lut5)
		    || (dev->pinw_req_for_cfg[i] == LI_D6
			&& dev->u.logic.a2d[LUT_D].lut5
			&& *dev->u.logic.a2d[LUT_D].lut5)) {
			rc = fnet_route_to_inpins(tstate->model, pinw_nets[i], "VCC_WIRE");
			if (rc) FAIL(rc);
		}
	}

	if ((rc = diff_printf(tstate))) FAIL(rc);

	for (i = 0; i < dev->pinw_req_total; i++)
		fnet_delete(tstate->model, pinw_nets[i]);
	fdev_delete(tstate->model, y, x, DEV_LOGIC, type_idx);
	return 0;
fail:
	return rc;
}

static int test_lut_encoding(struct test_state* tstate)
{
	int idx_enum[] = { DEV_LOG_M_OR_L, DEV_LOG_X };
	int x_enum[] = { /*xm*/ 13, /* center-xl*/ 22, /*xl*/ 39 };
	int y, x_i, i, j, k, lut_str_len, rc;
	int type_i, lut;
	char lut_str[128];
	const char* lut5_parents[] = {"(A6+~A6)*1", "(A6+~A6)*0",
		"(A6+~A6)*A1", "(A6+~A6)*A3", 0};

	tstate->diff_to_null = 1;

	y = 68;
	for (x_i = 0; x_i < sizeof(x_enum)/sizeof(*x_enum); x_i++) {
		for (type_i = 0; type_i < sizeof(idx_enum)/sizeof(*idx_enum); type_i++) {
			for (lut = LUT_A; lut <= LUT_D; lut++) {
				// lut6 only
				rc = test_lut(tstate, y, x_enum[x_i], idx_enum[type_i],
					lut, "0", /*lut5*/ 0);
				if (rc) FAIL(rc);
				rc = test_lut(tstate, y, x_enum[x_i], idx_enum[type_i],
					lut, "1", /*lut5*/ 0);
				if (rc) FAIL(rc);
				for (i = '1'; i <= '6'; i++) {
					snprintf(lut_str, sizeof(lut_str), "A%c", i);
					rc = test_lut(tstate, y, x_enum[x_i], idx_enum[type_i],
						lut, lut_str, /*lut5*/ 0);
					if (rc) FAIL(rc);
				}
				for (i = 0; i < 64; i++) {
					lut_str_len = 0;
					for (j = 0; j < 6; j++) {
						if (lut_str_len)
							lut_str[lut_str_len++] = '*';
						if (!(i & (1<<j)))
							lut_str[lut_str_len++] = '~';
						lut_str[lut_str_len++] = 'A';
						lut_str[lut_str_len++] = '1' + j;
					}
					lut_str[lut_str_len] = 0;
					rc = test_lut(tstate, y, x_enum[x_i], idx_enum[type_i],
						lut, lut_str, /*lut5*/ 0);
					if (rc) FAIL(rc);
				}
				// lut6 and lut5 pairs
				i = 0;
				while (lut5_parents[i]) {
					rc = test_lut(tstate, y, x_enum[x_i], idx_enum[type_i],
						lut, /*lut6*/ lut5_parents[i], /*lut5*/ "0");
					if (rc) FAIL(rc);
					rc = test_lut(tstate, y, x_enum[x_i], idx_enum[type_i],
						lut, /*lut6*/ lut5_parents[i], /*lut5*/ "1");
					if (rc) FAIL(rc);
					for (j = '1'; j <= '5'; j++) {
						snprintf(lut_str, sizeof(lut_str), "A%c", j);
						rc = test_lut(tstate, y, x_enum[x_i], idx_enum[type_i],
							lut, /*lut6*/ lut5_parents[i], /*lut5*/ lut_str);
						if (rc) FAIL(rc);
					}
					for (j = 0; j < 32; j++) {
						lut_str_len = 0;
						for (k = 0; k < 5; k++) {
							if (lut_str_len)
								lut_str[lut_str_len++] = '*';
							if (!(j & (1<<k)))
								lut_str[lut_str_len++] = '~';
							lut_str[lut_str_len++] = 'A';
							lut_str[lut_str_len++] = '1' + k;
						}
						lut_str[lut_str_len] = 0;
						rc = test_lut(tstate, y, x_enum[x_i], idx_enum[type_i],
							lut, /*lut6*/ lut5_parents[i], /*lut5*/ lut_str);
						if (rc) FAIL(rc);
					}
					i++;
				}
			}
		}
	}
out:
	return 0;
fail:
	return rc;
}

#define DEFAULT_DIFF_EXEC "./autotest_diff.sh"

static void printf_help(const char* argv_0, const char** available_tests)
{
	printf( "\n"
		"fpgatools automatic test suite\n"
		"\n"
		"Usage: %s [--test=<name>] [--skip=<num>] [--dry-run]\n"
		"       %*s [--diff=<diff executable>]\n"
		"Default diff executable: " DEFAULT_DIFF_EXEC "\n", argv_0, (int) strlen(argv_0), "");

	if (available_tests) {
		int i = 0;
		printf("\n");
		while (available_tests[i] && available_tests[i][0]) {
			printf("%s %s\n", !i ? "Available tests:"
				: "                ", available_tests[i]);
			i++;
		}
	}
	printf("\n");
}

int main(int argc, char** argv)
{
	struct fpga_model model;
	struct test_state tstate;
	char param[1024], cmdline_test[1024];
	int i, param_skip, rc;
	const char* available_tests[] =
		{ "logic_cfg", "routing_sw", "io_sw", "iob_cfg", "lut_encoding", 0 };

	// flush after every line is better for the autotest
	// output, tee, etc.
	// for example: ./autotest 2>&1 | tee autotest.log
	setvbuf(stdout, /*buf*/ 0, _IOLBF, /*size*/ 0);

	if (argc < 2) {
		printf_help(argv[0], available_tests);
		return 0;
	}

	//
	// command line
	//

	memset(&tstate, 0, sizeof(tstate));
	tstate.cmdline_skip = -1;
	tstate.cmdline_diff_exec[0] = 0;
	cmdline_test[0] = 0;
	tstate.dry_run = -1;
	tstate.diff_to_null = 0;
	for (i = 1; i < argc; i++) {
		memset(param, 0, sizeof(param));
		if (sscanf(argv[i], "--test=%1023c", param) == 1) {
			if (cmdline_test[0]) {
				printf_help(argv[0], available_tests);
				return EINVAL;
			}
			strcpy(cmdline_test, param);
			continue;
		}
		memset(param, 0, sizeof(param));
		if (sscanf(argv[i], "--diff=%1023c", param) == 1) {
			if (tstate.cmdline_diff_exec[0]) {
				printf_help(argv[0], available_tests);
				return EINVAL;
			}
			strcpy(tstate.cmdline_diff_exec, param);
			continue;
		}
		if (sscanf(argv[i], "--skip=%i", &param_skip) == 1) {
			if (tstate.cmdline_skip != -1) {
				printf_help(argv[0], available_tests);
				return EINVAL;
			}
			tstate.cmdline_skip = param_skip;
			continue;
		}
		if (!strcmp(argv[i], "--dry-run")) {
			tstate.dry_run = 1;
			continue;
		}
		printf_help(argv[0], available_tests);
		return EINVAL;
	}
	if (!cmdline_test[0]) {
		printf_help(argv[0], available_tests);
		return EINVAL;
	}
	i = 0;
	while (available_tests[i] && available_tests[i][0]) {
		if (!strcmp(available_tests[i], cmdline_test))
			break;
		i++;
	}
	if (!available_tests[i] || !available_tests[i][0]) {
		printf_help(argv[0], available_tests);
		return EINVAL;
	}
	if (!tstate.cmdline_diff_exec[0])
		strcpy(tstate.cmdline_diff_exec, DEFAULT_DIFF_EXEC);
	if (tstate.cmdline_skip == -1)
		tstate.cmdline_skip = 0;
	if (tstate.dry_run == -1)
		tstate.dry_run = 0;

	//
	// test
	//

	printf("\n");
	printf("O fpgatools automatic test suite. Be welcome and be "
			"our guest. namo namaha.\n");
	printf("\n");
	printf("O Test: %s\n", cmdline_test);
	printf("O Diff: %s\n", tstate.cmdline_diff_exec);
	printf("O Skip: %i\n", tstate.cmdline_skip);
	printf("O Dry run: %i\n", tstate.dry_run);
	printf("\n");
	printf("O Time measured in seconds from 0.\n");
	g_start_time = time(0);
	TIMESTAMP();
	printf("O Memory usage reported in megabytes.\n");
	MEMUSAGE();

	printf("O Building memory model...\n");
	if ((rc = fpga_build_model(&model, XC6SLX9_ROWS, XC6SLX9_COLUMNS,
			XC6SLX9_LEFT_WIRING, XC6SLX9_RIGHT_WIRING)))
		goto fail;
	printf("O Done\n");
	TIME_AND_MEM();

	tstate.model = &model;
	strcpy(tstate.tmp_dir, AUTOTEST_TMP_DIR);
	mkdir(tstate.tmp_dir, S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH);
	rc = diff_start(&tstate, cmdline_test);
	if (rc) FAIL(rc);

	if (!strcmp(cmdline_test, "logic_cfg")) {
		rc = test_logic_config(&tstate);
		if (rc) FAIL(rc);
	}
	if (!strcmp(cmdline_test, "routing_sw")) {
		rc = test_routing_switches(&tstate);
		if (rc) FAIL(rc);
	}
	if (!strcmp(cmdline_test, "io_sw")) {
		rc = test_iologic_switches(&tstate);
		if (rc) FAIL(rc);
	}
	if (!strcmp(cmdline_test, "iob_cfg")) {
		rc = test_iob_config(&tstate);
		if (rc) FAIL(rc);
	}
	if (!strcmp(cmdline_test, "lut_encoding")) {
		rc = test_lut_encoding(&tstate);
		if (rc) FAIL(rc);
	}

	printf("\n");
	printf("O Test completed.\n");
	TIME_AND_MEM();
	printf("\n");
	return EXIT_SUCCESS;
fail:
	return rc;
}
