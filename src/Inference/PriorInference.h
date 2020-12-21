#pragma once

#include "Control.h"
#include <queue>
#include <thread>

#include "ParallelInferenceInterface.h"

/**
 * @class PriorInference
 * @author piantado
 * @date 10/06/20
 * @file PriorInference.h
 * @brief Inference by sampling from the prior -- doesn't tend to work well, but might be a useful baseline
 */
template<typename HYP, typename callback_t>
class PriorInference : public ParallelInferenceInterface<> {

public:
	
	typename HYP::Grammar_t* grammar;
	typename HYP::data_t* data;
	callback_t* callback; // must be static so it can be accessed in GraphNode
	HYP* from;
	
	PriorInference(typename HYP::Grammar_t* g, typename HYP::data_t* d, callback_t& cb, HYP* fr=nullptr) : 
		grammar(g), data(d), callback(&cb), from(fr) {
	}
	
	void run_thread(Control ctl) override {

		ctl.start();
		while(ctl.running()) {
			HYP h;
			if(from != nullptr) {
				h = *from; // copy 
				h.complete();
			}
			else {
				h = MyHypothesis::make(grammar);
			}
			h.compute_posterior(*data);
			(*callback)(h);
		}
	}
};