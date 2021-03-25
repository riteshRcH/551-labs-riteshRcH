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

#include "ctcp.h"
#include "ctcp_linked_list.h"
#include "ctcp_sys.h"
#include "ctcp_utils.h"

typedef struct {
  uint32_t sequence_number_last_accepted;
  bool got_fin_flag_set_segment;
  linked_list_t* output_pending_segments;
} receiver_state_t;

typedef struct {
  uint32_t most_recent_received_acknowledgement;
  bool got_ctrl_d;
  uint32_t most_recent_sequence_number;
  uint32_t most_recent_sequence_number_sent;
  linked_list_t* wrapped_unacknowledged_segs;
} sender_state_t;

typedef struct {
  uint32_t         retransmit_counter;
  long             last_send_time;
  ctcp_segment_t   ctcp_segment;
} wrapped_ctcp_segment_t;

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
  long FIN_WAIT_start_time;

  ctcp_config_t ctcp_config;
  sender_state_t tx_state;
  receiver_state_t rx_state;
};

/**
 * Linked list of connection states. Go through this in ctcp_timer() to
 * resubmit segments and tear down connections.
 */
static ctcp_state_t *state_list;

void ctcp_send(ctcp_state_t *state);

void ctcp_send_segment(ctcp_state_t *state, wrapped_ctcp_segment_t* wrapped_segment);

void ctcp_send_info_segment(ctcp_state_t *state);

ctcp_state_t *ctcp_init(conn_t *conn, ctcp_config_t *cfg)
{
  /* Connection could not be established. */
  if (conn == NULL) {
    return NULL;
  }

  /* Established a connection. Create a new state and update the linked list
     of connection states. */
  ctcp_state_t *state = calloc(sizeof(ctcp_state_t), 1);
  state->next = state_list;
  state->prev = &state_list;
  if (state_list)
    state_list->prev = &state->next;
  state_list = state;

  /* Set fields. */
  state->conn = conn;
  state->FIN_WAIT_start_time = 0;

  state->ctcp_config.recv_window = cfg->recv_window, state->ctcp_config.send_window = cfg->send_window, state->ctcp_config.timer = cfg->timer, state->ctcp_config.rt_timeout = cfg->rt_timeout;
  state->tx_state.most_recent_received_acknowledgement = 0, state->tx_state.got_ctrl_d = false, state->tx_state.most_recent_sequence_number = 0, state->tx_state.most_recent_sequence_number_sent = 0, state->tx_state.wrapped_unacknowledged_segs = ll_create();
  state->rx_state.sequence_number_last_accepted = 0, state->rx_state.got_fin_flag_set_segment = false, state->rx_state.output_pending_segments = ll_create();

  free(cfg);
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

    for (i = 0; i < ll_length(state->tx_state.wrapped_unacknowledged_segs); i++)
    {
      ll_node_t *head = ll_front(state->tx_state.wrapped_unacknowledged_segs);
      free(head->object);
      ll_remove(state->tx_state.wrapped_unacknowledged_segs, head);
    }
    ll_destroy(state->tx_state.wrapped_unacknowledged_segs);

    for (i = 0; i < ll_length(state->rx_state.output_pending_segments); ++i)
    {
      ll_node_t *head = ll_front(state->rx_state.output_pending_segments);
      free(head->object);
      ll_remove(state->rx_state.output_pending_segments, head);
    }
    ll_destroy(state->rx_state.output_pending_segments);

    free(state);
  }
  end_client();
}

void ctcp_read(ctcp_state_t *state)
{
  uint8_t buf[MAX_SEG_DATA_SIZE];
  int read_bytes_cnt;
  wrapped_ctcp_segment_t* new_seg_pointer;

  if (state->tx_state.got_ctrl_d)
    return;

  while ((read_bytes_cnt = conn_input(state->conn, buf, MAX_SEG_DATA_SIZE)) > 0)
  {
    new_seg_pointer = (wrapped_ctcp_segment_t*) calloc(1, sizeof(wrapped_ctcp_segment_t) + read_bytes_cnt);
    new_seg_pointer->ctcp_segment.len = htons((uint16_t) sizeof(ctcp_segment_t) + read_bytes_cnt), new_seg_pointer->ctcp_segment.seqno = htonl(state->tx_state.most_recent_sequence_number + 1);
    memcpy(new_seg_pointer->ctcp_segment.data, buf, read_bytes_cnt);
    state->tx_state.most_recent_sequence_number += read_bytes_cnt;
    ll_add(state->tx_state.wrapped_unacknowledged_segs, new_seg_pointer);
  }

  if (read_bytes_cnt == -1)
  {
    state->tx_state.got_ctrl_d = true;
    new_seg_pointer = (wrapped_ctcp_segment_t*) calloc(1, sizeof(wrapped_ctcp_segment_t));
    new_seg_pointer->ctcp_segment.len = htons((uint16_t) sizeof(ctcp_segment_t)), new_seg_pointer->ctcp_segment.seqno = htonl(state->tx_state.most_recent_sequence_number + 1), new_seg_pointer->ctcp_segment.flags |= TH_FIN;
    ll_add(state->tx_state.wrapped_unacknowledged_segs, new_seg_pointer);
  }

  ctcp_send(state_list);
}

void ctcp_send(ctcp_state_t *state)
{
  wrapped_ctcp_segment_t *pointer_to_wrapped_ctcp_seg;
  ll_node_t *current_node;
  unsigned int i, length;
  uint32_t most_recent_seg_sequence_number, max_valid_sequence_number;

  if (state == NULL)
    return;

  length = ll_length(state->tx_state.wrapped_unacknowledged_segs);
  if (length == 0)
    return;

  for (i = 0; i < length; ++i)
  {
    if (i == 0) current_node = ll_front(state->tx_state.wrapped_unacknowledged_segs);
    else current_node = current_node->next;

    pointer_to_wrapped_ctcp_seg = (wrapped_ctcp_segment_t *) current_node->object, most_recent_seg_sequence_number = ntohl(pointer_to_wrapped_ctcp_seg->ctcp_segment.seqno) + (ntohs(((ctcp_segment_t *) (&pointer_to_wrapped_ctcp_seg->ctcp_segment))->len) - sizeof(ctcp_segment_t)) - 1, max_valid_sequence_number = state->tx_state.most_recent_received_acknowledgement - 1 + state->ctcp_config.send_window;

    if (state->tx_state.most_recent_received_acknowledgement == 0)
      max_valid_sequence_number++;

    if (most_recent_seg_sequence_number > max_valid_sequence_number)
      return;

    if (pointer_to_wrapped_ctcp_seg->retransmit_counter == 0)
      ctcp_send_segment(state, pointer_to_wrapped_ctcp_seg);
    else if (i == 0)
    {
      if ((current_time() - pointer_to_wrapped_ctcp_seg->last_send_time) > state->ctcp_config.rt_timeout)
        ctcp_send_segment(state, pointer_to_wrapped_ctcp_seg);
    }
  }
}

void ctcp_send_segment(ctcp_state_t *state, wrapped_ctcp_segment_t* wrapped_segment)
{
  uint16_t seg_checksum;
  int sent_bytes_count;

  if (wrapped_segment->retransmit_counter >= 6)
  {
    ctcp_destroy(state);
    return;
  }

  wrapped_segment->ctcp_segment.ackno = htonl(state->rx_state.sequence_number_last_accepted + 1), wrapped_segment->ctcp_segment.flags |= TH_ACK, wrapped_segment->ctcp_segment.window = htons(state->ctcp_config.recv_window), wrapped_segment->ctcp_segment.cksum = 0, seg_checksum = cksum(&wrapped_segment->ctcp_segment, ntohs(wrapped_segment->ctcp_segment.len)), wrapped_segment->ctcp_segment.cksum = seg_checksum;

  sent_bytes_count = conn_send(state->conn, &wrapped_segment->ctcp_segment, ntohs(wrapped_segment->ctcp_segment.len));
  wrapped_segment->retransmit_counter++;

  if (sent_bytes_count < ntohs(wrapped_segment->ctcp_segment.len))
    return;

  if (sent_bytes_count == -1)
  {
    ctcp_destroy(state);
    return;
  }

  state->tx_state.most_recent_sequence_number_sent += sent_bytes_count, wrapped_segment->last_send_time = current_time();
}


void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len)
{
  uint16_t calculated_checksum, got_checksum, data_byte_count;
  uint32_t most_recent_seg_sequence_number, largest_valid_sequence_number, smallest_valid_sequence_number;
  unsigned int length, i;
  ll_node_t* ptr;
  ctcp_segment_t* seg_pointer;

  if (len < ntohs(segment->len))
  {
    free(segment);
    return;
  }

  got_checksum = segment->cksum, segment->cksum = 0, calculated_checksum = cksum(segment, ntohs(segment->len)), segment->cksum = got_checksum;
  if (got_checksum != calculated_checksum)
  {
    free(segment);
    return;
  }

  data_byte_count = ntohs(segment->len) - sizeof(ctcp_segment_t);

  if (data_byte_count)
  {
    most_recent_seg_sequence_number = ntohl(segment->seqno) + data_byte_count - 1, smallest_valid_sequence_number = state->rx_state.sequence_number_last_accepted + 1, largest_valid_sequence_number = state->rx_state.sequence_number_last_accepted
      + state->ctcp_config.recv_window;

    if ((most_recent_seg_sequence_number > largest_valid_sequence_number) || (ntohl(segment->seqno) < smallest_valid_sequence_number))
    {
      free(segment);
      ctcp_send_info_segment(state);
      return;
    }
  }

  if (segment->flags & TH_ACK)
    state->tx_state.most_recent_received_acknowledgement = ntohl(segment->ackno);


  if (data_byte_count || (segment->flags & TH_FIN))
  {
    length = ll_length(state->rx_state.output_pending_segments);

    if (length == 0)
      ll_add(state->rx_state.output_pending_segments, segment);
    else if (length == 1)
    {
      ptr = ll_front(state->rx_state.output_pending_segments);
      seg_pointer = (ctcp_segment_t*) ptr->object;
      if (ntohl(segment->seqno) == ntohl(seg_pointer->seqno)) free(segment);
      else if (ntohl(segment->seqno) > ntohl(seg_pointer->seqno)) ll_add(state->rx_state.output_pending_segments, segment);
      else ll_add_front(state->rx_state.output_pending_segments, segment);
    }
    else
    {
      ctcp_segment_t* head_seg_pointer;
      ctcp_segment_t* tail_seg_pointer;
      ll_node_t* head_node_pointer;
      ll_node_t* tail_node_pointer;

      head_node_pointer = ll_front(state->rx_state.output_pending_segments), tail_node_pointer  = ll_back(state->rx_state.output_pending_segments), head_seg_pointer = (ctcp_segment_t*) head_node_pointer->object, tail_seg_pointer  = (ctcp_segment_t*) tail_node_pointer->object;

      if (ntohl(segment->seqno) > ntohl(tail_seg_pointer->seqno)) ll_add(state->rx_state.output_pending_segments, segment);
      else if (ntohl(segment->seqno) < ntohl(head_seg_pointer->seqno)) ll_add_front(state->rx_state.output_pending_segments, segment);
      else
      {
        for (i = 0; i < (length-1); ++i)
        {
          ll_node_t* current_node;
          ll_node_t* next_node_ptr;
          ctcp_segment_t* curr_ctcp_segment_ptr;
          ctcp_segment_t* next_ctcp_segment_ptr;

          if (i == 0) current_node = ll_front(state->rx_state.output_pending_segments);
          else current_node = current_node->next;
          next_node_ptr = current_node->next, curr_ctcp_segment_ptr = (ctcp_segment_t*) current_node->object, next_ctcp_segment_ptr = (ctcp_segment_t*) next_node_ptr->object;

          if ((ntohl(segment->seqno) == ntohl(curr_ctcp_segment_ptr->seqno)) || (ntohl(segment->seqno) == ntohl(next_ctcp_segment_ptr->seqno)))
          {
            free(segment);
            break;
          }else
          {
            if ((ntohl(segment->seqno) > ntohl(curr_ctcp_segment_ptr->seqno)) && (ntohl(segment->seqno) < ntohl(next_ctcp_segment_ptr->seqno)))
            {
              ll_add_after(state->rx_state.output_pending_segments, current_node, segment);
              break;
            }
          }
        }
      }
    }
  }
  else
    free(segment);

  ctcp_output(state);

  ll_node_t* head;
  wrapped_ctcp_segment_t* pointer_to_wrapped_ctcp_seg;
  uint32_t last_byte_sequence_number;
  uint16_t data_bytes_count;

  while (ll_length(state->tx_state.wrapped_unacknowledged_segs) != 0)
  {
    head = ll_front(state->tx_state.wrapped_unacknowledged_segs), pointer_to_wrapped_ctcp_seg = (wrapped_ctcp_segment_t*) head->object, data_bytes_count = ntohs(pointer_to_wrapped_ctcp_seg->ctcp_segment.len) - sizeof(ctcp_segment_t), last_byte_sequence_number = ntohl(pointer_to_wrapped_ctcp_seg->ctcp_segment.seqno) + data_bytes_count - 1;

    if (last_byte_sequence_number < state->tx_state.most_recent_received_acknowledgement)
    {
      free(pointer_to_wrapped_ctcp_seg);
      ll_remove(state->tx_state.wrapped_unacknowledged_segs, head);
    }else
      return;
  }
}

void ctcp_output(ctcp_state_t *state)
{
  ll_node_t* head;
  ctcp_segment_t* seg_pointer;
  size_t bufspace;
  int data_byte_count;
  int val;
  int num_segments_output = 0;

  if (state == NULL)
    return;

  while (ll_length(state->rx_state.output_pending_segments) != 0)
  {
    head = ll_front(state->rx_state.output_pending_segments);
    seg_pointer = (ctcp_segment_t*) head->object;

    data_byte_count = ntohs(seg_pointer->len) - sizeof(ctcp_segment_t);
    if (data_byte_count)
    {
      if ( ntohl(seg_pointer->seqno) != state->rx_state.sequence_number_last_accepted + 1)
        return;

      bufspace = conn_bufspace(state->conn);
      if (bufspace < data_byte_count)
        return;

      val = conn_output(state->conn, seg_pointer->data, data_byte_count);
      if (val == -1)
      {
        ctcp_destroy(state);
        return;
      }
      num_segments_output++;
    }

    if (data_byte_count)
      state->rx_state.sequence_number_last_accepted += data_byte_count;

    if ((!state->rx_state.got_fin_flag_set_segment) && (seg_pointer->flags & TH_FIN))
    {
      state->rx_state.got_fin_flag_set_segment = true;
      state->rx_state.sequence_number_last_accepted++;
      conn_output(state->conn, seg_pointer->data, 0);
      num_segments_output++;
    }

    free(seg_pointer);
    ll_remove(state->rx_state.output_pending_segments, head);
  }

  if (num_segments_output)
    ctcp_send_info_segment(state);
}

void ctcp_send_info_segment(ctcp_state_t *state)
{
  ctcp_segment_t ctcp_seg;
  ctcp_seg.ackno = htonl(state->rx_state.sequence_number_last_accepted + 1), ctcp_seg.seqno = htonl(0), ctcp_seg.len   = sizeof(ctcp_segment_t), ctcp_seg.flags = TH_ACK;
  ctcp_seg.window = htons(state->ctcp_config.recv_window), ctcp_seg.cksum = 0, ctcp_seg.cksum = cksum(&ctcp_seg, sizeof(ctcp_segment_t));

  conn_send(state->conn, &ctcp_seg, sizeof(ctcp_segment_t));
}

void ctcp_timer()
{
  ctcp_state_t * current_state;

  if (state_list == NULL) return;

  current_state = state_list;
  while (current_state != NULL)
  {
    ctcp_output(current_state);
    ctcp_send(current_state);

    if ((current_state->rx_state.got_fin_flag_set_segment) && (current_state->tx_state.got_ctrl_d) && (ll_length(current_state->tx_state.wrapped_unacknowledged_segs) == 0) && (ll_length(current_state->rx_state.output_pending_segments) == 0))
    {
    	if ((current_time() - current_state->FIN_WAIT_start_time) > (2*60000))
    		ctcp_destroy(current_state);
      	else if (current_state->FIN_WAIT_start_time == 0)
        	current_state->FIN_WAIT_start_time = current_time();        
    }
    current_state = current_state->next;
  }
}
