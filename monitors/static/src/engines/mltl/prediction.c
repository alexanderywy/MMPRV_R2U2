#include "r2u2.h"
#include "prediction.h"
#include <stdlib.h>
#include <math.h>

#if R2U2_PRED_PROB

r2u2_status_t find_child_instructions(r2u2_monitor_t *monitor, r2u2_instruction_t *instr, r2u2_mltl_instruction_t** mltl_instructions, size_t *mltl_size, r2u2_instruction_t** load_instructions, size_t *load_size, uint8_t difference){
  if(instr->engine_tag == R2U2_ENG_TL) {
    r2u2_mltl_instruction_t* mltl_instr = (r2u2_mltl_instruction_t*)instr->instruction_data;
    switch (mltl_instr->opcode){
      case R2U2_MLTL_OP_FT_LOAD: {
        if(difference > 0){
          return R2U2_OK;
        } else { // No booleanizer or atomic checker
          r2u2_mltl_instruction_t* load_instr = ((r2u2_mltl_instruction_t*)instr->instruction_data);
          // Only store one instance of each load instruction for a single atomic (e.g., don't include loading a0 and Pr(a0))
          for(int i = 0; i < *load_size; i++){
            r2u2_mltl_instruction_t* prev_load_instr = ((r2u2_mltl_instruction_t*)load_instructions[i]->instruction_data);
            if(load_instr->op1_value == prev_load_instr->op1_value){
              return R2U2_OK;
            }
          }
          load_instructions[*load_size] = instr;
          *load_size = *load_size + 1;
          return R2U2_OK;
        }
      }
      case R2U2_MLTL_OP_FT_RETURN: {
        instr = &(*monitor->instruction_tbl)[mltl_instr->op1_value+difference];
        mltl_instructions[0] = (r2u2_mltl_instruction_t*)(instr->instruction_data);
        *mltl_size = 1;
        return find_child_instructions(monitor, instr, mltl_instructions, mltl_size, load_instructions, load_size, difference);
      }
      case R2U2_MLTL_OP_FT_GLOBALLY:
      case R2U2_MLTL_OP_FT_NOT: 
      case R2U2_MLTL_OP_FT_PROB:{
        if(mltl_instr->op1_type == R2U2_FT_OP_ATOMIC || mltl_instr->op1_type == R2U2_FT_OP_SUBFORMULA){
          instr = &(*monitor->instruction_tbl)[mltl_instr->op1_value+difference];
          mltl_instructions[*mltl_size] = (r2u2_mltl_instruction_t*)(instr->instruction_data);
          *mltl_size = *mltl_size + 1;
          return find_child_instructions(monitor, instr, mltl_instructions, mltl_size, load_instructions, load_size, difference);
        }else{
          return R2U2_OK;
        }
      }
      case R2U2_MLTL_OP_FT_UNTIL:
      case R2U2_MLTL_OP_FT_AND: {
        r2u2_status_t status = R2U2_OK;
        if(mltl_instr->op1_type == R2U2_FT_OP_ATOMIC || mltl_instr->op1_type == R2U2_FT_OP_SUBFORMULA){
          instr = &(*monitor->instruction_tbl)[mltl_instr->op1_value+difference];
          mltl_instructions[*mltl_size] = (r2u2_mltl_instruction_t*)(instr->instruction_data);
          *mltl_size = *mltl_size + 1;
          status = find_child_instructions(monitor, instr, mltl_instructions, mltl_size, load_instructions, load_size, difference);
        }
        if(status == R2U2_OK && (mltl_instr->op2_type == R2U2_FT_OP_ATOMIC || mltl_instr->op2_type == R2U2_FT_OP_SUBFORMULA)){
          instr = &(*monitor->instruction_tbl)[mltl_instr->op2_value+difference];
          mltl_instructions[*mltl_size] = (r2u2_mltl_instruction_t*)(instr->instruction_data);
          *mltl_size = *mltl_size + 1;
          return find_child_instructions(monitor, instr, mltl_instructions, mltl_size, load_instructions, load_size, difference);
        }else{
          return status;
        }
      }
      case R2U2_MLTL_OP_FT_EVENTUALLY:
      case R2U2_MLTL_OP_FT_RELEASE:
      case R2U2_MLTL_OP_FT_OR:
      case R2U2_MLTL_OP_FT_IMPLIES:
      case R2U2_MLTL_OP_FT_NOR:
      case R2U2_MLTL_OP_FT_XOR:
      case R2U2_MLTL_OP_FT_EQUIVALENT: {
        return R2U2_UNIMPL;
      }
      case R2U2_MLTL_OP_FT_NOP: {
        return R2U2_OK;
      }
      default: {
        // Somehow got into wrong tense dispatch
        return R2U2_INVALID_INST;
      }
    }
  }
  return R2U2_OK;
}

void prep_prediction_scq(r2u2_monitor_t *monitor, r2u2_mltl_instruction_t** instructions, r2u2_mltl_instruction_t* return_instr, r2u2_scq_state_t* prev_real_state, size_t size){
  R2U2_DEBUG_PRINT("-----------------Starting New Round of Prediction (at time stamp %d)-----------------\n", monitor->time_stamp);
  r2u2_duoq_arena_t *arena = &(monitor->duo_queue_mem);
  for(size_t i = 0; i < size; i++){
    r2u2_duoq_control_block_t *ctrl = &(arena->blocks[instructions[i]->memory_reference]);
    r2u2_duoq_temporal_block_t *temp = r2u2_duoq_ft_temporal_get(arena, instructions[i]->memory_reference);
    prev_real_state[i].read1 = ctrl->read1;
    prev_real_state[i].read2 = ctrl->read2;
    prev_real_state[i].next_time = ctrl->next_time;
    prev_real_state[i].edge = temp->edge;
    prev_real_state[i].previous = temp->previous;
    ctrl->pred_write = ctrl->write;
  }
  r2u2_duoq_control_block_t *ctrl_return_instr = &(arena->blocks[return_instr->memory_reference]);
  prev_real_state[size].read1 = ctrl_return_instr->read1;
  ctrl_return_instr->pred_write = ctrl_return_instr->write;
}

void restore_scq(r2u2_monitor_t *monitor, r2u2_mltl_instruction_t** instructions, r2u2_mltl_instruction_t* return_instr, r2u2_scq_state_t* prev_real_state, size_t size){
  r2u2_duoq_arena_t *arena = &(monitor->duo_queue_mem);
  for(size_t i = 0; i < size; i++){
    r2u2_duoq_control_block_t *ctrl = &(arena->blocks[instructions[i]->memory_reference]);
    r2u2_duoq_temporal_block_t *temp = r2u2_duoq_ft_temporal_get(arena, instructions[i]->memory_reference);
    ctrl->read1 = prev_real_state[i].read1;
    ctrl->read2 = prev_real_state[i].read2;
    ctrl->next_time = prev_real_state[i].next_time;
    temp->edge = prev_real_state[i].edge;
    temp->previous = prev_real_state[i].previous;
  }
  r2u2_duoq_control_block_t *ctrl_return_instr = &(arena->blocks[return_instr->memory_reference]);
  ctrl_return_instr->read1 = prev_real_state[size].read1;
  R2U2_DEBUG_PRINT("--------------------Ending Round of Prediction (at time step %d)---------------------\n",monitor->time_stamp);
}
#endif