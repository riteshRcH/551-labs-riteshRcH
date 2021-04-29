#include "ctcp_bbr.h"
#include <stdlib.h>

struct bbr* bbr_init_state()
{
	struct bbr *bbr = malloc(sizeof(struct bbr));

	bbr->rtts_happened = 0, bbr->currently_executing_mode = STARTUP, bbr->rtt_minimum_check_for_window_ms = 10000, bbr->bottleneck_bandwidth = 1440;
	bbr->pacing_gain = bbr_pacing_gain[0], bbr->transmit_time_for_next_packet = 0, bbr->rt_prop = 200;

	return bbr;
}

void on_ack_bbr_state_chk(struct bbr *bbr)
{
	switch(bbr->currently_executing_mode)
	{
		case STARTUP:
			if((bbr->rtts_happened % 3 == 0) && (bbr->startup_bw_least_recent != 0))
			{
				if(( (bbr->startup_bw_most_recent - bbr->startup_bw_least_recent)/bbr->startup_bw_least_recent *100) >25)
				{
					bbr->drain_begin = bbr->rtts_happened, bbr->currently_executing_mode = DRAIN;
					return;
				}
			}

			if((bbr->rtts_happened % 3 == 0) && (bbr->startup_bw_least_recent == 0))
			{
				bbr->drain_begin = bbr->rtts_happened, bbr->currently_executing_mode = DRAIN;
				return;
			}else
				bbr->pacing_gain = bbr_pacing_gain[1];

			bbr->cwnd =(float) (bbr->rt_prop) * bbr_pacing_gain[1] * bbr->bottleneck_bandwidth;
			break;

		case PROBE_RTT:
			if(bbr->rtts_happened - bbr->probe_rtt_begin >=4)
				bbr->data_in_probe_bw_mode =0, bbr->temp_cnter_for_probe_bw =2, bbr->currently_executing_mode = PROBE_BW;
			else
				bbr->pacing_gain = bbr_pacing_gain[5], bbr->cwnd = bbr->rt_prop * bbr->bottleneck_bandwidth * bbr_pacing_gain[1];
			break;

		case PROBE_BW:
			if(bbr->data_in_probe_bw_mode >= (bbr->rt_prop * bbr->bottleneck_bandwidth/1000))
			{
				if(bbr->temp_cnter_for_probe_bw == 8)
					bbr->temp_cnter_for_probe_bw = 1;
				bbr->temp_cnter_for_probe_bw++, bbr->data_in_probe_bw_mode = 0;
			}
			bbr->cwnd =  bbr->bottleneck_bandwidth * bbr->rt_prop * bbr_pacing_gain[5], bbr->pacing_gain = bbr_pacing_gain[bbr->temp_cnter_for_probe_bw];	
			break;

		case DRAIN:
			if( bbr->rtts_happened - bbr->drain_begin >=10)
				bbr->currently_executing_mode = PROBE_BW, bbr->data_in_probe_bw_mode =0, bbr->temp_cnter_for_probe_bw =2;
			else
			bbr->cwnd =  bbr->rt_prop * bbr_pacing_gain[2] * bbr->bottleneck_bandwidth, bbr->pacing_gain = bbr_pacing_gain[2];
			break;
	}
}