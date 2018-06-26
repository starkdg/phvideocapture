/**

MIT License

Copyright (c) 2018 David G. Starkweather 

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

**/

#include <cstdlib>
#include <iostream>
#include <atomic>
#include <thread>
#include <mutex>
#include <cassert>
#include <cstdint>
#include "circ_buf.h"

using namespace std;

const int CircBufferSize = 1024;  //size of circular buffer
const int NumberSamples = 100000000; //print values 0 ... Max-1 in producer thread to circ buffer

/** circular buffer type **/
typedef struct circ_buf {
	int16_t *samples;
	atomic_ulong head;
	atomic_ulong tail;
} CircBuffer;

mutex producer_mtx;
mutex consumer_mtx;

int produce(CircBuffer *circbuffer, int n){
	int16_t val = 0;
	long count = 0;
	while (count < NumberSamples){
		producer_mtx.lock();
		unsigned long head = circbuffer->head.load(memory_order_relaxed);
		unsigned long tail = circbuffer->tail.load(memory_order_acquire);
		if ((int)CIRC_SPACE(head, tail, CircBufferSize) >= n){
			for (int i=0;i<n;i++){
				circbuffer->samples[head] = val++;
				head = (head + 1) & (CircBufferSize - 1);
				count++;
			}
		}
		circbuffer->head.store(head, memory_order_release);
		producer_mtx.unlock();
	}
	cout << "produce: " << count << endl;
	return 0;
}

int consume(CircBuffer *circbuffer, int n){
	int16_t *local_buffer = new int16_t[n];
	long count = 0;
	int16_t val = 0;
	while (count < NumberSamples){
		consumer_mtx.lock();
		unsigned long head = circbuffer->head.load(memory_order_acquire);
		unsigned long tail = circbuffer->tail.load(memory_order_relaxed);
		if ((int)CIRC_CNT(head, tail, CircBufferSize) >= n){
			for (int i=0;i<n;i++){
				local_buffer[i] = circbuffer->samples[tail];
				tail = (tail + 1) & (CircBufferSize - 1);
				assert(val++ == local_buffer[i]);
				count++;
			}
			circbuffer->tail.store(tail, memory_order_release);
		}
		consumer_mtx.unlock();
	}
	cout << "consume: " << count << endl;
	delete[] local_buffer;
	return 0;
}


int main(int argc, char **argv){
	cout << "main:test circ buffer" << endl;
	
	CircBuffer circbuffer;
	circbuffer.samples = new int16_t[CircBufferSize];
	circbuffer.head = 0;
	circbuffer.tail = 0;

	cout << "main:start producer thread" << endl;
	thread producer_thr(produce, &circbuffer, 250);

	cout << "main:start consumer thread" << endl;
	thread consumer_thr(consume, &circbuffer, 1000);
	
	cout << "main:wait ..." << endl;
	producer_thr.join();
	consumer_thr.join();

	delete[] circbuffer.samples;
	cout << "main:Done." << endl;
	return 0;
}
