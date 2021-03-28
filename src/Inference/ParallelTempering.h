#pragma once 

//#define PARALLEL_TEMPERING_SHOW_DETAIL

#include "Errors.h"
#include <signal.h>
#include <functional>
#include "ChainPool.h"

extern volatile sig_atomic_t CTRL_C; 

/**
 * @class ParallelTempering
 * @author piantado
 * @date 10/06/20
 * @file ParallelTempering.h
 * @brief This is a chain pool that runs multiple chains on a ladder of different temperatures and adjusts temperatures in order to 
 * 			balances swaps up and down the ladder (which makes it efficient). 
 * 			The adaptation scheme follows https://arxiv.org/abs/1501.05823
 * 			NOTE This starts two extra threads, one for adapting and one for swapping, but they mostly wait around.
 * 		
 */

template<typename HYP, typename callback_t>
class ParallelTempering : public ChainPool<HYP,callback_t> {

	const unsigned long WAIT_AND_SLEEP = 250; // how many ms to wait between checking to see if it is time to swap/adapt
	
public:
	std::vector<double> temperatures;
	
	// Swap history stores how often the i'th chain swaps with the (i-1)'st chain
	FiniteHistory<bool>* swap_history;
	
	bool is_temperature; // set for whether we initialize according to a temperature ladder (true) or data
	
	std::atomic<bool> terminate; // used to kill swapper and adapter
	
	ParallelTempering(HYP& h0, typename HYP::data_t* d, callback_t& cb, std::initializer_list<double> t, bool allcallback=true) : 
		ChainPool<HYP,callback_t>(h0, d, cb, temperatures.size(),allcallback), temperatures(t), terminate(false) {
		
		// allcallback is true means that all chains call the callback, otherwise only t=0
		for(size_t i=0;i<temperatures.size();i++) {
			this->pool[i].temperature = temperatures[i]; // set its temperature 
		}
		
		swap_history = new FiniteHistory<bool>[temperatures.size()];
	}
	
	
	ParallelTempering(HYP& h0, typename HYP::data_t* d, callback_t& cb, unsigned long n, double maxT, bool allcallback=true) : 
		ChainPool<HYP,callback_t>(h0, d, cb, n, allcallback),terminate(false) {
		assert(n != 0);
		
		if(n == 1) {
			this->pool[0].temperature = 1.0;
		}
		else {
			for(size_t i=0;i<n;i++) {
				this->pool[i].temperature = exp(i * log(maxT)/(n-1));
			}
		}
		
		swap_history = new FiniteHistory<bool>[n];
	}
	
	
	~ParallelTempering() {
		delete[] swap_history;
	}
	
	void __swapper_thread(time_ms swap_every ) {
			
		// runs a swapper every swap_every ms (double)
		// NOTE: If we have 1 element in the pool, it is caught below
		auto last = now();
		while(!(terminate or CTRL_C)) {
			
			if(time_since(last) < swap_every or this->pool.size() <= 1){
				std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_AND_SLEEP));
			}
			else { // do a swap
				
				size_t k = 1+myrandom(this->pool.size()-1); // swap k with k-1

				// get both of these thread locks
				std::lock_guard guard1(this->pool[k-1].current_mutex);
				std::lock_guard guard2(this->pool[k  ].current_mutex);
				
				// compute R based on data
				double Tnow = this->pool[k-1].at_temperature(this->pool[k-1].temperature)   + this->pool[k].at_temperature(this->pool[k].temperature);
				double Tswp = this->pool[k-1].at_temperature(this->pool[k].temperature)     + this->pool[k].at_temperature(this->pool[k-1].temperature);
				double R = Tswp-Tnow;
			
				if(R >= 0 or uniform() < exp(R)) { 
										
					#ifdef PARALLEL_TEMPERING_SHOW_DETAIL
					COUT "# Swapping " <<k<< " and " <<(k-1)<<"." TAB this->pool[k].current.posterior TAB this->pool[k-1].current.posterior TAB this->pool[k].current.string() TAB this->pool[k-1].current.string() ENDL;
					#endif
					
					// swap the chains
					std::swap(this->pool[k].current, this->pool[k-1].current);
					
					swap_history[k] << true;
				}
				else {
					swap_history[k] << false;
				}
				
				last = now();
				
			}
		}
	}
	
	void __adapter_thread(time_ms adapt_every) {
		
		auto last = now();
		while(! (terminate or CTRL_C) ) {
			
			if(time_since(last) < adapt_every){
				std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_AND_SLEEP));
			}
			else {
				last = now();
				
				#ifdef PARALLEL_TEMPERING_SHOW_DETAIL
					show_statistics();
				#endif
				
				adapt(); // TOOD: Check what counts as t
			}
		}
	}
	
	
	void run(Control ctl) { throw NotImplementedError(); }
	void run(Control ctl, time_ms swap_every, time_ms adapt_every) {
		
		// Start a swapper and adapter thread
		std::thread swapper(&ParallelTempering<HYP,callback_t>::__swapper_thread, this, swap_every); // pass in the non-static mebers like this:
		std::thread adapter(&ParallelTempering<HYP,callback_t>::__adapter_thread, this, adapt_every);

		// run normal pool run
		ChainPool<HYP,callback_t>::run(ctl); // passing copies here
		
		// kill everything else once that thread is done
		terminate = true;
		
		swapper.join();
		adapter.join();
		
	}
	
	void show_statistics() {
		COUT "# Pool info: \n";
		for(size_t i=0;i<this->pool.size();i++) {
			std::lock_guard guard1(this->pool[i].current_mutex); // definitely need this to print
			COUT "# " << i TAB this->pool[i].temperature TAB this->pool[i].current.posterior TAB
					     this->pool[i].acceptance_ratio() TAB swap_history[i].N TAB this->pool[i].samples TAB this->pool[i].current.string()
						 ENDL;
		}
	}
	
	double k(unsigned long t, double v, double t0) {
		return (1.0/v) * t0/(t+t0); 
	}
	
	void adapt(double v=3, double t0=1000000) {
		//show_statistics();
		
		std::vector<double> sw(this->pool.size());
		
		for(size_t i=1;i<this->pool.size()-1;i++) { // never adjust i=0 (T=1) or the max temperature
			sw[i] = log(this->pool[i].temperature - this->pool[i-1].temperature);
			
			if( swap_history[i].N>0 && swap_history[i+1].N>0 ) { // only adjust if there are samples
				sw[i] += k(this->pool[i].samples, v, t0) * (swap_history[i].mean()-swap_history[i+1].mean()); 
			}
			
		}
		
		// Reset all of the swap histories (otherwise we keep adapting in a bad way)
		for(size_t i=1;i<this->pool.size();i++) { 
			swap_history[i].reset();
			swap_history[i] << true; swap_history[i] << false; // this is a little +1 smoothing
		}
		
		// and then convert S to temperatures again
		// but never adjust i=0 (T=1) OR the last one
		for(size_t i=1;i<this->pool.size()-1;i++) { 
			this->pool[i].temperature = this->pool[i-1].temperature + exp(sw[i]);
		}
	}
	
};



/**
 * @class DataTempering
 * @author piantado
 * @date 10/06/20
 * @file ParallelTempering.h
 * @brief This is like ParallelTempering but it tempers on the amount of data. Note then it doesn't adapt anything. 
 */
template<typename HYP, typename callback_t>
class DataTempering : public ChainPool<HYP,callback_t> {
	
	const unsigned long WAIT_AND_SLEEP = 250; // how many ms to wait between checking to see if it is time to swap/adapt
	
public:
	FiniteHistory<bool>* swap_history;
	std::atomic<bool> terminate; // used to kill swapper and adapter
	
	DataTempering(HYP& h0, std::vector<typename HYP::data_t>& datas, std::vector<callback_t>& cb)  : terminate(false) {
		assert(datas.size() == cb.size() && "*** Must provide equal length vectors of datas and callbacks");
		// This version anneals on data, giving each chain a different amount in datas order
		for(size_t i=0;i<datas.size();i++) {
			this->pool.push_back(MCMCChain(i==0?h0:h0.restart(), &(datas[i]), cb[i]));
			this->pool[i].temperature = 1.0;
		}

		swap_history = new FiniteHistory<bool>[datas.size()];
	}
	
	
	~DataTempering() {
		delete[] swap_history;
	}
	
	void __swapper_thread(time_ms swap_every ) {
		// runs a swapper every swap_every seconds (double)
		// NOTE: If we have 1 element in the pool, it is caught below
		auto last = now();
		while(!(terminate or CTRL_C)) {
			
			if(time_since(last) < swap_every or this->pool.size() <= 1){
				std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_AND_SLEEP));
			}
			else { // do a swap
				
				size_t k = 1+myrandom(this->pool.size()-1); // swap k with k-1

				// get both of these thread locks
				std::lock_guard guard1(this->pool[k-1].current_mutex);
				std::lock_guard guard2(this->pool[k  ].current_mutex);
				
				// must compute swaps based on data
				double Pnow = this->pool[k-1].current.posterior + this->pool[k].current.posterior;
				
				// make copies and compute posterior on each other's data
				HYP x = this->pool[k-1].current; x.compute_posterior(*this->pool[k].data);
				HYP y = this->pool[k].current;   y.compute_posterior(*this->pool[k-1].data);
				double Pswap = x.posterior + y.posterior;				
				double R = Pswap - Pnow;
				
				if(R >= 0 or uniform() < exp(R)) { 
										
					#ifdef DATA_TEMPERING_SHOW_DETAIL
					COUT "# Swapping " <<k<< " and " <<(k-1)<<"." TAB this->pool[k].current.posterior TAB this->pool[k-1].current.posterior TAB this->pool[k].current.string() TAB this->pool[k-1].current.string() ENDL;
					#endif
					
					// swap the chains
					std::swap(this->pool[k].current, this->pool[k-1].current);
					
					// and if we're doing data, we must recompute
					this->pool[k].current.compute_posterior(*this->pool[k].data);
					this->pool[k-1].current.compute_posterior(*this->pool[k-1].data);
					
					swap_history[k] << true;
				}
				else {
					swap_history[k] << false;
				}
				
				last = now();
			}
		}
	}
	
	
	virtual void run(Control ctl) override { throw NotImplementedError(); }
	virtual void run(Control ctl, time_ms swap_every, time_ms adapt_every) {
		
		std::thread swapper(&ParallelTempering<HYP,callback_t>::__swapper_thread, this, swap_every); // pass in the non-static mebers like this:
		
		ChainPool<HYP,callback_t>::run(ctl);
		
		terminate = true;
		
		swapper.join();		
	}
	
	void show_statistics() {
		COUT "# Pool info: \n";
		for(size_t i=0;i<this->pool.size();i++) {
			COUT "# " << i TAB this->pool[i].temperature TAB this->pool[i].current.posterior TAB
					     this->pool[i].acceptance_ratio() TAB swap_history[i].mean() TAB this->pool[i].samples TAB this->pool[i].current.string()
						 ENDL;
		}
	}
	
	
};