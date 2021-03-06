#include "fpga_api.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cstring>

#define min(x, y) (((x) < (y)) ? (x) : (y))

FPGA::FPGA(off_t data_addr, off_t output_addr, int m_size, int v_size)
{
  m_size_ = m_size;
  v_size_ = v_size;
  data_size_ = (m_size_ + 1) * v_size_ * sizeof(float); // fpga bram data size

  fd_ = open("/dev/mem", O_RDWR);
  data_ = static_cast<float *>(mmap(NULL, data_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, data_addr));
  output_ = static_cast<unsigned int *>(mmap(NULL, sizeof(unsigned int), PROT_READ | PROT_WRITE, MAP_SHARED, fd_, output_addr));

  num_block_call_ = 0;
}

FPGA::~FPGA()
{
  munmap(data_, data_size_);
  munmap(output_, sizeof(unsigned int));
  close(fd_);
}

float *FPGA::matrix(void)
{
  return data_ + v_size_;
}

float *FPGA::vector(void)
{
  return data_;
}

void FPGA::reset(void)
{
  num_block_call_ = 0;
}

int FPGA::num_block_call(void)
{
  return num_block_call_;
}

const float *__attribute__((optimize("O0"))) FPGA::blockMV()
{
  num_block_call_ += 1;

  // fpga version
  *output_ = 0x5555;
  while (*output_ == 0x5555)
    ;

  return data_;
}

void FPGA::largeMV(const float *large_mat, const float *input, float *output, int num_input, int num_output)
{
  float *vec = this->vector();
  float *mat = this->matrix();

  // 0) Initialize output vector
  for (int i = 0; i < num_output; ++i)
    output[i] = 0;
  bool *flag = new bool[num_input];
  for(int i=0; i<num_input; i++)
  {
      if(input[i] == 0.00000f) flag[i] = false; // zero skip.
      else {
          flag[i] = true; // non zero -> true
      }
  }

  float *out = new float[num_output];
  for (int j = 0; j < num_input; )
  {
        int block_col = min(v_size_, num_input-j);
        int idx = 0, k = 0;
        
        while( ((j+k) < num_input) && (idx < block_col) ){
            if(flag[j+k]) vec[idx++] = input[j+k];

            k++;
        }

    for (int i = 0; i < num_output; i += m_size_)
    {
        // 0) Initialize input vector		
        int block_row = min(m_size_, num_output-i);
        
        int kk=0; idx = 0;
        while( ((j+kk) < num_input) && (idx < block_col) ){
            if(flag[j+kk]) {
                for(int mm = 0; mm < block_row; mm++)
                {
                    mat[mm * v_size_ + idx] = large_mat[(i+mm)*num_input + j + kk];
                }
                vec[idx++] = input[j+kk];
            }
            kk++;
        }
        if(block_row < m_size_)
        {
            for(int x = 0; x < m_size_ - block_row ; x++)
            {
                memset(mat+ v_size_ * (block_row + x ), 0,  sizeof(float) * v_size_);
            }
        }
        // 3) Call a function `block_call() to execute MV multiplication
        const float* ret = this->blockMV();

        // 4) Accumulate intermediate results
        for(int row = 0; row < block_row; ++row)
        {
            output[i + row] += ret[row];
        }
    }

    j += k;
  }

  delete[] flag;

}

void FPGA::convLowering(const std::vector<std::vector<std::vector<std::vector<float>>>> &cnn_weights,
                        std::vector<std::vector<float>> &new_weights,
                        const std::vector<std::vector<std::vector<float>>> &inputs,
                        std::vector<std::vector<float>> &new_inputs)
{
  /*
   * Arguments:
   *
   * conv_weights: [conv_channel, input_channel, conv_height, conv_width]
   * new_weights: [?, ?]
   * inputs: [input_channel, input_height, input_width]
   * new_inputs: [?, ?]
   *
   */

  int conv_channel = cnn_weights.size();
  int input_channel = cnn_weights[0].size();
  int conv_height = cnn_weights[0][0].size();
  int conv_width = cnn_weights[0][0][0].size();
  //int input_channel = inputs.size();
  int input_height = inputs[0].size();
  int input_width = inputs[0][0].size();
  // IMPLEMENT THIS
  // For example,
  // new_weights[0][0] = cnn_weights[0][0][0][0];
  // new_inputs[0][0] = inputs[0][0][0];
    for(int c=0; c<conv_channel; c++)
    {
      // vector row new_weight[c] 
      int cnt = 0;
      for(int ic = 0; ic<input_channel; ic++)
      {
          for(int h=0; h<conv_height; h++)
          {
              for(int w=0; w<conv_width; w++)
              {
                  new_weights[c][cnt++] = (cnn_weights[c][ic][h][w]);
              }
          }
      }
    }

  // first move row-wise  
  int cnt = 0;
  for(int y=0; y<input_height-conv_height+1; y++)
  {
      for(int x=0; x<input_width-conv_width+1; x++)
      {
          // input row new_input[cnt];
          for(int ic=0; ic<input_channel; ic++)
          {
                for(int h=0; h<conv_height; h++)
                {
                    for(int w=0; w<conv_width; w++)
                    {
                        int idx = w + conv_width * h + (conv_width*conv_height) * ic;
                        new_inputs[idx][cnt] = inputs[ic][h+y][w+x]; 
                    }
                }
          }
        cnt++;
      }

  }
}