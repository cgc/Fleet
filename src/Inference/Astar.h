#pragma once

#include "IO.h"
#include "Control.h"
#include <queue>
#include <thread>
#include <signal.h>

#include "FleetStatistics.h"
#include "ParallelInferenceInterface.h"

//#define DEBUG_ASTAR 1

extern volatile sig_atomic_t CTRL_C; 

/**
 * @class Astar
 * @author piantado
 * @date 07/06/20
 * @file Astar.h
 * @brief This is an implementation of kinda-A* search that maintains a priority queue of partial states and attempts to find 
 * 			a program with the lowest posterior score. To do this, we choose a node to expand based on its prior plus
 * 		    N_REPS samples of its likelihood, computed by filling in its children at random. This stochastic heuristic is actually
 *  		inadmissable since it usually overestimates the cost. As a result, it usually makes sense to run
 *  		A* at a pretty high temperature, corresponding to a downweighting of the likelihood, and making the heuristic more likely
 * 			to be admissable. 
 * 
 * 			One general challenge is how to handle -inf likelihoods, and here we've done that by, if you end up with -inf, taking
 * 		    your parent's temperature and multiplying by PARENT_PENALTY. 
 * 
 * 
 */
template<typename HYP, typename callback_t>
class Astar :public ParallelInferenceInterface {
	
	std::mutex lock; 
public:
	static constexpr size_t INITIAL_SIZE = 10000000;
	static constexpr size_t N_REPS = 1; // how many times do we try randomly filling in to determine priority? 
	static constexpr double PARENT_PENALTY = 1.1;
	static double temperature;	
	static callback_t* callback; // must be static so it can be accessed in GraphNode
	static typename HYP::data_t* data;
	
	// the smallest element appears on top of this vector
	std::priority_queue<HYP, ReservedVector<HYP,INITIAL_SIZE>> Q;
	 
	Astar(HYP& h0, typename HYP::data_t* d, callback_t& cb, double temp) {
		
		// set these static members (static so GraphNode can use them without having so many copies)
		callback = &cb; 
		data = d;
		temperature = temp;
		
		// We're going to make sure we don't start on -inf because this value will
		// get inherited by my kisd for when they use -inf
		for(size_t i=0;i<1000;i++) {
			auto g = h0;
			g.complete();
			
			g.compute_posterior(*data);
			(*callback)(g);
			if(g.likelihood > -infinity) {
				h0.prior = 0.0;
				h0.likelihood = g.likelihood / temperature;				
				push(h0);
				break;
			}
		}
		
		assert(not Q.empty() && "*** You should have pushed a non -inf value into Q above -- are you sure its possible?");
	}
	
	void push(HYP& h) {
		std::lock_guard guard(lock);
		Q.push(h);
	}
	
	void push(HYP&& h) {
		std::lock_guard guard(lock);
		Q.push(std::move(h));
	}
	
	void run_thread(Control ctl) override {

		ctl.start();
		while(ctl.running()) {
			
			lock.lock();
			if(Q.empty()) {
				lock.unlock();
				continue; // this is necesssary because we might have more threads than Q to start off. 
			}
			auto t = Q.top(); Q.pop(); ++FleetStatistics::astar_steps;
			lock.unlock();
			
			#ifdef DEBUG_ASTAR
				CERR std::this_thread::get_id() TAB "ASTAR popped " << t.posterior TAB t.string() ENDL;
			#endif
			
			size_t neigh = t.neighbors();
			assert(neigh>0); // we should not have put on things with no neighbors
			for(size_t k=0;k<neigh;k++) {
				auto v = t.make_neighbor(k);
				
				// if v is a terminal, we callback
				// otherwise we 
				if(v.is_evaluable()) {
					v.compute_posterior(*data); 
					(*callback)(v);
				}
				else {
					
					// We compute a stochastic heuristic by filling in h some number of times and taking the min
					// we're going to start with the previous likelihood we found -- NOTE That this isn't an admissable
					// heuristic, and may not even make sense, but seems to work well, meainly in preventing -inf
					double likelihood = t.likelihood * PARENT_PENALTY;  // remember this likelihood is divided by temperature
					for(size_t i=0;i<N_REPS and not CTRL_C;i++) {
						auto g = v; g.complete();
						g.compute_posterior(*data);
						(*callback)(g);
						if(not std::isnan(g.likelihood)) {
							likelihood = std::max(likelihood, g.likelihood / temperature); // temperature must go here
						}
					}
					 
					// now we save this prior and likelihood to v (even though v is a partial tree)
					// so that it is sorted (defaultly by posterior) 
					v.likelihood = likelihood; // save so we can use next time
					v.posterior  = v.compute_prior() + v.likelihood;
					//CERR "POST:" TAB v.string() TAB v.posterior TAB likelihood ENDL;
					
					
					push(std::move(v));
				}
			}
			
			// this condition can't go at the top because of multithreading -- we need each thread to loop in the top when it's
			// waiting at first for the Q to fill up
			if(Q.empty()) break; 
		}
	}
	
	
	
	
};

template<typename HYP, typename callback_t>
callback_t* Astar<HYP,callback_t>::callback = nullptr;

template<typename HYP, typename callback_t>
typename HYP::data_t* Astar<HYP,callback_t>::data = nullptr;

template<typename HYP, typename callback_t>
double Astar<HYP,callback_t>::temperature = 100.0;