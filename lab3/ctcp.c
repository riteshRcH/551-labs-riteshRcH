/******************************************************************************
 * ctcp.c
 * ------
 * Implementation of cTCP done here. This is the only file you need to change.
 * Look at the following files for references and useful functions:
 *   - ctcp.h: Headers for this file.
 *   - ctcp_iinked_list.h: Linked list functions for managing a linked list.
 *   - ctcp_sys.h: Connection-related structs and functions, cTCP segment
 *                 definition.
 *   - ctcp_utils.h: Checksum computation, getting the current time.
 *
 *****************************************************************************/

#include "ctcp_sys.h"
#include "ctcp.h"
#include "ctcp_linked_list.h"
#include "ctcp_utils.h"
/**
 * Connection state.
 *
 * Stores per-connection information such as the current sequence number,
 * unacknowledged packets, etc.
 *
 * You should add to this to store other fields you might need.
 */
struct ctcp_state {
  struct ctcp_state *next;  /* Next in linked list */
  struct ctcp_state **prev; /* Prev in linked list */

  conn_t *conn;             /* Connection object -- needed in order to figure
                               out destination when sending */
  linked_list_t *segments;  /* Linked list of segments sent to this connection.
                               It may be useful to have multiple linked lists
                               for unacknowledged segments, segments that
                               haven't been sent, etc. Lab 1 uses the
                               stop-and-wait protocol and therefore does not
                               necessarily need a linked list. You may remove
                               this if this is the case for you */

  
  struct bbr *bbr;

  ctcp_config_t *ctcp_config; 
  FILE *bdp_file_pointer; 

  uint32_t most_recent_sequence_number; 
  uint32_t sequence_number_last_accepted; 

  uint32_t most_recent_received_acknowledgement; 
  uint32_t second_most_recent_acknowledgement; 

  uint16_t send_window; 
  uint16_t recv_window; 
  uint16_t sliding_window_size_bytes; 

  unsigned int connection_state; 

  unsigned int last_ctcp_timer_call; 

  long last_send_time; 

  long minimum_rtt_captured; 
  unsigned int maximum_bandwidth_captured; 

  unsigned int inflight_data; 
  unsigned int app_limited_until; 

  linked_list_t *unacknowledged_segments; 
  linked_list_t *output_pending_segments; 
};

/**
 * Linked list of connection states. Go through this in ctcp_timer() to
 * resubmit segments and tear down connections.
 */
static ctcp_state_t *state_list;


ctcp_state_t *ctcp_init(conn_t *conn, ctcp_config_t *ctcp_config)
{
  /* Connection could not be established. */
  if (conn == NULL) {
    return NULL;
  }

  /* Established a connection. Create a new state and update the linked list
     of connection states. */
  struct ctcp_state *state = calloc(sizeof(ctcp_state_t), 1);
  state->next = state_list;
  state->prev = &state_list;
  if (state_list)
    state_list->prev = &state->next;
  state_list = state;
  
  state->conn = conn, state->ctcp_config = ctcp_config;
  state->most_recent_sequence_number = 1, state->most_recent_received_acknowledgement = 1, state->second_most_recent_acknowledgement = 1, state->sequence_number_last_accepted = 1;
  state->recv_window = ctcp_config->recv_window, state->send_window = ctcp_config->send_window;
  state->unacknowledged_segments = ll_create(), state->output_pending_segments = ll_create();
  state->connection_state = 0, state->sliding_window_size_bytes = 0, state->app_limited_until=0, state->last_send_time =0, state->inflight_data = 0, state->last_ctcp_timer_call =0;
  state->minimum_rtt_captured =-1, state->maximum_bandwidth_captured = -1;
  
  state->bdp_file_pointer  = fopen("bdp.txt","w");
  state->bbr = bbr_init_state();
  state->bbr->cwnd = state->send_window;
  state->bbr->currently_executing_mode = STARTUP;

  //free(ctcp_config);
  return state;
}

void ctcp_destroy(ctcp_state_t *state)
{
  unsigned int i;
  if (state)
  {
    if (state->next) state->next->prev = state->prev;
    *state->prev = state->next;
    conn_remove(state->conn);

    for(i = 0; i < ll_length(state->unacknowledged_segments); i++)
    {
      ll_node_t *curr_node = ll_front(state->unacknowledged_segments);
      free(curr_node->object);
      ll_remove(state->unacknowledged_segments, curr_node);
    }
    ll_destroy(state->unacknowledged_segments);

    for(i = 0; i < ll_length(state->output_pending_segments); i++)
    {
      ll_node_t *curr_node = ll_front(state->output_pending_segments);
      free(curr_node->object);
      ll_remove(state->output_pending_segments, curr_node);
    }
    ll_destroy(state->output_pending_segments);

    free(state);
  }
  end_client();
}

void send_segment(ctcp_state_t *state,char *data,int data_size,int flags,int req_ack)
{
  ctcp_segment_t *send_segment = (ctcp_segment_t*)calloc(sizeof(ctcp_segment_t) + data_size,1);
  int send_segment_size = sizeof(ctcp_segment_t) + data_size;
  memcpy(send_segment->data,data,data_size);
  if(flags!=-1)
    send_segment->flags = flags;
  
  send_segment->seqno = htonl(state->most_recent_sequence_number);
  send_segment->ackno = htonl(state->most_recent_received_acknowledgement);
  send_segment->len = htons(send_segment_size);
  send_segment->window= htons(state->send_window);
  send_segment->cksum = 0;
  send_segment->cksum = cksum(send_segment,send_segment_size);
  if(req_ack == 1)
  {
    void **arr = malloc(5 * sizeof(void *));
    arr[0] = malloc(sizeof(ctcp_segment_t*));
    arr[1] = malloc(sizeof(long));
    arr[2] = malloc(sizeof(int));
    arr[3] = malloc(sizeof(int));
    arr[4] = malloc(sizeof(int));
    arr[0] = send_segment;
    *((int *)(arr[2])) = 0;
    *((int *)(arr[3])) = 0;
    *((int *)(arr[4])) = 0;
    
    ll_add(state->output_pending_segments,arr);
    state->most_recent_sequence_number = state->most_recent_sequence_number + data_size;
 
    if(ll_length(state->output_pending_segments)>0)
    {   
      while((state->sliding_window_size_bytes < state->send_window) &&(ll_length(state->output_pending_segments)>0))
      {
        ll_add(state->unacknowledged_segments,ll_front(state->output_pending_segments)->object);
        void **buf = (ll_front(state->output_pending_segments)->object);
        ctcp_segment_t *seg = (ctcp_segment_t*)buf[0];
        state->sliding_window_size_bytes = state->sliding_window_size_bytes + ntohs(seg->len);
        ll_remove(state->output_pending_segments,ll_front(state->output_pending_segments));
      }
    }
    int can_send_how_much = state->sliding_window_size_bytes - state->inflight_data;
    int bdp = (state->bbr->rt_prop)* state->bbr->bottleneck_bandwidth * 2.89;
    ll_node_t *current_node;
    
    if(state->inflight_data >= bdp && bdp!=0)
      return;
    
    if((ll_length(state->output_pending_segments)== 0) && (can_send_how_much ==0))
    {
      state->app_limited_until = state->inflight_data;
      return;
    }
    
    for(current_node = state->unacknowledged_segments->head; current_node!=NULL; current_node = current_node->next)
    {
      void **buf = current_node->object;
      ctcp_segment_t *segment = (ctcp_segment_t*)buf[0];
      if(*((int *)(buf[2])) == 0)
      {
        *((long *)(buf[1])) = state->last_send_time = current_time();
        if(state->app_limited_until >0)
          *((int *)(buf[4])) = 1;
        usleep(2000);
        while(1)
        {
          if(current_time() >= state->bbr->transmit_time_for_next_packet)
          {
            state->bbr->data_in_probe_bw_mode += ntohs(segment->len);
            conn_send(state->conn,segment,ntohs(segment->len));
            break;
          }
        }
        *((int *)(buf[2])) = 1;
        
        state->inflight_data += ntohs(segment->len);
        if ((state->maximum_bandwidth_captured != -1) && (state->maximum_bandwidth_captured !=0))
          state->bbr->transmit_time_for_next_packet = current_time() + ntohs(segment->len) /(state->bbr->pacing_gain  * state->bbr->bottleneck_bandwidth);
        else
          state->bbr->transmit_time_for_next_packet = 0;
      }
    }
  }
  else
  {
    usleep(2500);
    
    conn_send(state->conn,send_segment,send_segment_size);
    state->most_recent_sequence_number = state->most_recent_sequence_number + data_size;
    free(send_segment);
    free(data);   
  }  
}

void ctcp_read(ctcp_state_t *state)
{  
  char stdin_data[MAX_SEG_DATA_SIZE];
  int stdin_read_size;
  int i;
  while(1)
  {   
    stdin_read_size = conn_input(state->conn,stdin_data,MAX_SEG_DATA_SIZE); 
    if(stdin_read_size > state->send_window)
    {
      for(i=0;i<stdin_read_size;i += state->send_window)
        send_segment(state,stdin_data+i,state->send_window,TH_ACK,1);           
    }
    
    if ((stdin_read_size == -1))
    {
      if(ll_length(state->output_pending_segments)!=0)
        return;
      if(state->connection_state ==0)
        send_segment(state,NULL,0,TH_FIN,0);
      else
      {
        send_segment(state,NULL,0,TH_FIN,0);
        ctcp_destroy(state);
      }
      return;
    }
    
    if(stdin_read_size == 0)
      break;
    send_segment(state,stdin_data,stdin_read_size,TH_ACK,1);
  }
  return;
}

void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len)
{
  int dup_flag =0;
  segment->cksum =0;
    
  if(((segment->flags & TH_FIN) == TH_FIN) &&(state->connection_state ==0))
  {
    state->connection_state = 1;
    send_segment(state,NULL,0,TH_ACK,0);
    conn_output(state->conn,segment->data,0);
    return;
  }
  else if(state->connection_state == 1)
  {
    ctcp_destroy(state);
    return;
  }
  
  if((segment->flags & TH_ACK) == TH_ACK)
  {
    if(state->connection_state == 1)
      ctcp_destroy(state);

    while(ll_length(state->unacknowledged_segments)>0)
    {
      ll_node_t *seg_node = ll_front(state->unacknowledged_segments);
      void **buf = (seg_node->object);
      ctcp_segment_t *seg = (ctcp_segment_t*)buf[0];     
      if(ntohl(seg->seqno) < ntohl(segment->ackno))
      {
        long rtt = current_time() - *((long*)buf[1]); 
        if(state->minimum_rtt_captured ==-1)
        {
          state->minimum_rtt_captured = rtt;
          state->bbr->rt_prop = rtt;
        }
        else
        {
          
          if(rtt <= state->minimum_rtt_captured)
          {
            state->minimum_rtt_captured = rtt;
            
            if(state->bbr->currently_executing_mode == PROBE_BW)
              state->bbr->currently_executing_mode = STARTUP;
          }
        }
        if(state->bbr->rtts_happened == 0)
        {
          state->bbr->rtts_happened++;
          state->maximum_bandwidth_captured = state->bbr->bottleneck_bandwidth = rtt ? ((float)((float)ntohs(seg->len)/(float)rtt)) : 0.0;
        }
        else
        {
          if(*((long*)buf[4]) ==0)
          {
            static int cnt;
            cnt++;
            if(cnt == 1)
              fprintf(state->bdp_file_pointer,"%ld,%d\n",current_time(),0);
            state->bbr->rtts_happened++;
            float bw = rtt ? ((float)((float)ntohs(seg->len)/(float)rtt)) : 0.0;
            
            fprintf(state->bdp_file_pointer,"%ld,%lld\n",current_time(),state->bbr->bottleneck_bandwidth*rtt*8);
            fflush(state->bdp_file_pointer);

            
            if(state->maximum_bandwidth_captured < bw)
            {
              state->maximum_bandwidth_captured = bw;
            }
            
            if(state->bbr->rtts_happened % 10 == 0)
            {
              if(state->bbr->bottleneck_bandwidth < state->maximum_bandwidth_captured)
                state->bbr->bottleneck_bandwidth = state->maximum_bandwidth_captured;
            }
          }
        }
        
        on_ack_bbr_state_chk(state->bbr);
        state->send_window =state->ctcp_config->send_window= state->bbr->cwnd;
        if(state->app_limited_until > 0)
          state->app_limited_until = state->app_limited_until - ntohs(seg->len);
        state->sliding_window_size_bytes = state->sliding_window_size_bytes - ntohs(seg->len);
        state->inflight_data = state->inflight_data - ntohs(seg->len);
        free(ll_remove(state->unacknowledged_segments,seg_node));
        
        if(ll_length(state->output_pending_segments) > 0)
        {
          if(ll_length(state->output_pending_segments)>0)
          {   
            while((state->sliding_window_size_bytes < state->send_window) &&(ll_length(state->output_pending_segments)>0))
            {
              ll_add(state->unacknowledged_segments,ll_front(state->output_pending_segments)->object);
              void **buf = (ll_front(state->output_pending_segments)->object);
              ctcp_segment_t *seg = (ctcp_segment_t*)buf[0];
              state->sliding_window_size_bytes = state->sliding_window_size_bytes + ntohs(seg->len);
              ll_remove(state->output_pending_segments,ll_front(state->output_pending_segments));
            }
          }
          int can_send_how_much = state->sliding_window_size_bytes - state->inflight_data;
          int bdp = (state->bbr->rt_prop)* state->bbr->bottleneck_bandwidth * 2.89;
          ll_node_t *current_node;
          
          if(state->inflight_data >= bdp && bdp!=0)
            return;
          
          if((ll_length(state->output_pending_segments)== 0) && (can_send_how_much ==0))
          {
            state->app_limited_until = state->inflight_data;
            return;
          }
          
          for(current_node = state->unacknowledged_segments->head; current_node!=NULL; current_node = current_node->next)
          {
            void **buf = current_node->object;
            ctcp_segment_t *segment = (ctcp_segment_t*)buf[0];
            if(*((int *)(buf[2])) == 0)
            {
              *((long *)(buf[1])) = state->last_send_time = current_time();
              if(state->app_limited_until >0)
                *((int *)(buf[4])) = 1;
              usleep(2000);
              while(1)
              {
                if(current_time() >= state->bbr->transmit_time_for_next_packet)
                {
                  state->bbr->data_in_probe_bw_mode += ntohs(segment->len);
                  conn_send(state->conn,segment,ntohs(segment->len));
                  break;
                }
              }
              *((int *)(buf[2])) = 1;
              
              state->inflight_data += ntohs(segment->len);
              if ((state->maximum_bandwidth_captured != -1) && (state->maximum_bandwidth_captured !=0))
                state->bbr->transmit_time_for_next_packet = current_time() + ntohs(segment->len) /(state->bbr->pacing_gain  * state->bbr->bottleneck_bandwidth);
              else
                state->bbr->transmit_time_for_next_packet = 0;
            }
          }
        }
      }
      else 
      {
        break;
      }
    }
  }
  int data_len = ntohs(segment->len)-sizeof(ctcp_segment_t);
  state->most_recent_received_acknowledgement = ntohl(segment->seqno) + data_len;
  if(ntohl(segment->seqno) <   state->second_most_recent_acknowledgement)
  {
    dup_flag = 1;
  }
  else
  {
    state->second_most_recent_acknowledgement = state->most_recent_received_acknowledgement;
    dup_flag =0;
  }
  char *segment_data_buf = (char*)malloc(sizeof(char)*MAX_SEG_DATA_SIZE);
  strncpy(segment_data_buf,segment->data,data_len);

  
  if(ntohs(segment->len) > sizeof(ctcp_segment_t))
  { 
    if(dup_flag ==0)
    {
      send_segment(state,NULL,0,TH_ACK,0);
      conn_output(state->conn,segment_data_buf,data_len);
    }
  }
   free(segment);
   free(segment_data_buf);
}

void ctcp_output(ctcp_state_t *state)
{
  send_segment(state,NULL,0,TH_ACK,0);
}

void ctcp_timer()
{  
  ctcp_state_t *curr_state = state_list;
  while (curr_state!= NULL)
  {   
    if(curr_state->last_ctcp_timer_call ==0)
      curr_state->last_ctcp_timer_call = current_time();

    if(curr_state->bbr->rtt_minimum_check_for_window_ms > 0) 
      curr_state->bbr->rtt_minimum_check_for_window_ms = curr_state->bbr->rtt_minimum_check_for_window_ms - (current_time() - curr_state->last_ctcp_timer_call), curr_state->last_ctcp_timer_call = current_time();

    if(curr_state->bbr->rtt_minimum_check_for_window_ms <0)
    {
      if( curr_state->bbr->currently_executing_mode == PROBE_RTT)
        curr_state->bbr->rt_prop = curr_state->minimum_rtt_captured;

      if((curr_state->bbr->rt_prop <= current_time() - curr_state->bbr->rtt_change_timestamp) && curr_state->bbr->currently_executing_mode == PROBE_BW)
          curr_state->bbr->probe_rtt_begin = curr_state->bbr->rtts_happened, curr_state->bbr->currently_executing_mode = PROBE_RTT;

      if(curr_state->minimum_rtt_captured < curr_state->bbr->rt_prop)
      {
        curr_state->bbr->rt_prop = curr_state->minimum_rtt_captured;

        if(curr_state->bbr->currently_executing_mode == PROBE_RTT)
          curr_state->bbr->currently_executing_mode = STARTUP;

        if(curr_state->bbr->currently_executing_mode == PROBE_BW)
            curr_state->bbr->probe_rtt_begin = curr_state->bbr->rtts_happened, curr_state->bbr->currently_executing_mode = PROBE_RTT;
      }
      if(curr_state->minimum_rtt_captured < curr_state->bbr->rt_prop)
        curr_state->bbr->rt_prop = curr_state->minimum_rtt_captured, curr_state->bbr->rtt_change_timestamp = current_time();
      
      curr_state->bbr->rtt_minimum_check_for_window_ms = 10000;
    }

    ll_node_t *curr_segment;
    for(curr_segment = curr_state->unacknowledged_segments->head; curr_segment!=NULL; curr_segment=curr_segment->next)
    {
      void **buf = (curr_segment->object);
      if((( current_time() - *((long*)buf[1]) > curr_state->ctcp_config->rt_timeout)&& (*((int*)buf[3])<5)))
      {
        ctcp_segment_t *seg = (ctcp_segment_t*)buf[0];
        *((long*)buf[1]) = current_time();
        curr_state->sliding_window_size_bytes = curr_state->sliding_window_size_bytes - ntohs(seg->len);
        *((int*)buf[3]) = *((int*)buf[3]) + 1;
        conn_send(curr_state->conn,seg,ntohs(seg->len));
      }
      
      if(*((int*)buf[3]) >=5)
      {
        ctcp_destroy(curr_state);
        break;
      }
    }
    curr_state=curr_state->next;
  }
}