enum bbr_mode {
	STARTUP,
	DRAIN,
	PROBE_BW,
	PROBE_RTT,
};

struct bbr
{
	char currently_executing_mode;
	long long rtts_happened;
	long long rtt_change_timestamp;
	long long cwnd;
	long rtt_minimum_check_for_window_ms;
	long transmit_time_for_next_packet;

	long long bottleneck_bandwidth;
	long rt_prop;
	float pacing_gain;

	long long startup_bw_most_recent;
	long long startup_bw_second_least_recent;
	long long startup_bw_least_recent;
	int drain_begin;
	long long temp_cnter_for_probe_bw;
	long long data_in_probe_bw_mode;
	int probe_rtt_begin;
};

static const float bbr_pacing_gain[] = {2.89, 1/2.89, 5/4, 3/4, 1, 1, 1, 1, 1, 1};
struct bbr* bbr_init_state();
void on_ack_bbr_state_chk(struct bbr *bbr);