/* 

 * Some ideas:
 * 
 * 	For a node, we stor ethe last *likelihood*. We then sample the next child with the current prior and the last likelihood. 
 * 
 * 	what if we preferentially follow leaves that have a *variable* likelihood distribution. Otherwise we might find something good 
 *  and then follow up on minor modifications of it? This would automatically be handled by doing some kind of UCT/percentile estimate
 * 	
 *	Maybe if we estimate quantiles rather than means or maxes, this will take care of this? If you tak ethe 90th or 99th quantile, 
 *  then you'll naturally start with 
 * 
 *  We should be counting gaps against hypotheses in the selection -- the gaps really make us bound the prior, since each gap must be filled. 
 * 
 * TODO: Should be using FAME NO OS as in the paper
 * 
 * 
 * 	Another idea: what if we sample from the bottom of the tree according to probability? To do that we sample and then sum up the lses (excluding closed nodes)
 * 		and then recurse down until we find what we need
 * 
 * 		-- Strange if we just do the A* version, then we'll have a priority queue and that's a lot liek sampling from the leaves? 
 * 
 * */
#pragma once 

#define DEBUG_MCTS 0

#include <atomic>
#include <mutex>
#include <set>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <functional>

#include "StreamingStatistics.h"
#include "ParallelInferenceInterface.h"
#include "Control.h"
#include "SpinLock.h"
#include "Random.h"

#include "BaseNode.h"

extern double explore; 

/**
 * @class FullMCTSNode
 * @author piantado
 * @date 03/06/20
 * @file FullMCTS.h
 * @brief This monte care tree search always plays out a full tree. It is mostly optimized to keep the tree as small as possible. 
 * 		To do this, it doesn't even store the value (HYP) in the tree, it just constructs it as a search step goes down the tree. 
 * 	    It also doesn't store parent pointers, it just passes them down the tree. We use SpinLock here because std::mutex is much
 *      bigger. Finally, explore, data, and callback are all static variables, which means that you will need to subclass this
 *      if you want to let those vary across nodes (or parallel runs). 
 * 		This lets us cram everything into 56 bytes. 
 * 
 * 		NOTE we could optimize a few more things -- for instance, storing the open bool either in nvisits, or which_expansion
 * 
 * 		TODO: We also store terminals currently in the tree, but there's no need for that -- we should eliminate that. 
 */

template<typename this_t, typename HYP, typename callback_t>
class FullMCTSNode : public ParallelInferenceInterface<HYP>, public BaseNode<this_t> {
	friend class BaseNode<this_t>;
		
public:

	using data_t = typename HYP::data_t;

	bool open; // am I still an available node?
		
	SpinLock mylock; 
	
	int which_expansion; 
	
	// these are static variables that makes them not have to be stored in each node
	// this means that if we want to change them for multiple MCTS nodes, we need to subclass
	static double explore; 
	static data_t* data;
	static callback_t* callback; 

	unsigned int nvisits;  // how many times have I visited each node?
	float max;
	float min;
	float lse;
	float last_lp;
    
	FullMCTSNode() {
	}
	
    FullMCTSNode(HYP& start, this_t* par,  size_t w) : 
		BaseNode<this_t>(0,par,0),
		open(true), which_expansion(w), nvisits(0), max(-infinity), min(infinity), lse(-infinity), last_lp(-infinity) {
		// here we don't expand the children because this is the constructor called when enlarging the tree
		mylock.lock();
			
		this->reserve_children(start.neighbors());
		mylock.unlock();
    }
    
    FullMCTSNode(HYP& start, double ex, data_t* d, callback_t& cb) : 
		BaseNode<this_t>(),
		open(true), which_expansion(0), nvisits(0), max(-infinity), min(infinity), lse(-infinity), last_lp(-infinity) {
		// This is the constructor that gets called from main, and it sets the static variables. All the other constructors
		// should use the above one
		
        mylock.lock();
		
		this->reserve_children(start.neighbors());
		explore = ex; 
		data = d;
		callback = &cb;
		
		mylock.unlock();
    }
	
	// should not copy or move because then the parent pointers get all messed up 
	FullMCTSNode(const this_t& m) { // because what happens to the child pointers?
		throw YouShouldNotBeHereError("*** should not be copying or moving MCTS nodes");
	}
	
	FullMCTSNode(this_t&& m) {
		// This must be defined for us to emplace_Back, but we don't actually want to move because that messes with 
		// the multithreading. So we'll throw an exception. You can avoid moves by reserving children up above, 
		// which should make it so that we never call this		
		throw YouShouldNotBeHereError("*** This must be defined for children.emplace_back, but should never be called");
	}
		
	void operator=(const FullMCTSNode& m) {
		throw YouShouldNotBeHereError("*** This must be defined for but should never be called");
	}
	void operator=(FullMCTSNode&& m) {
		throw YouShouldNotBeHereError("*** This must be defined for but should never be called");
	}
    
	void print(HYP from, std::ostream& o, const int depth, const bool sort) { 
		// here from is not a reference since we want to copy when we recurse 
		
        std::string idnt = std::string(depth, '\t'); // how far should we indent?
        
		std::string opn = (open?" ":"*");
		
		o << idnt TAB opn TAB last_lp TAB max TAB "visits=" << nvisits TAB which_expansion TAB from ENDL;
		
		// optional sort
		if(sort) {
			// we have to make a copy of our pointer array because otherwise its not const			
			std::vector<std::pair<this_t*,int>> c2;
			int w = 0;
			for(auto& c : this->get_children()) {
				c2.emplace_back(&c,w);
				++w;
			}
			std::sort(c2.begin(), 
					  c2.end(), 
					  [](const auto a, const auto b) {
							return a.first->nvisits > b.first->nvisits;
					  }
					  ); // sort by how many samples

			for(auto& c : c2) {
				HYP newfrom = from; newfrom.expand_to_neighbor(c.second);
				c.first->print(newfrom, o, depth+1, sort);
			}
		}
		else {		
			int w = 0;
			for(auto& c : this->get_children()) {
				HYP newfrom = from; newfrom.expand_to_neighbor(w);
				c.print(newfrom, o, depth+1, sort);
				++w;;
			}
		}
    }
 
	// wrappers for file io
    void print(HYP& start, const bool sort=true) { 
		print(start, std::cout, 0, sort);		
	}

	void print(HYP& start, const char* filename, const bool sort=true) {
		std::ofstream out(filename);
		print(start, out, 0, sort);
		out.close();		
	}

    void add_sample(const float v) {
        max = std::max(max,v);	
		if(v != -infinity) // keep track of the smallest non-inf value -- we use this instead of inf for sampling
			min = std::min(min, v); 
		lse = logplusexp(lse, v);
		last_lp = v;
		
		// and propagate up the tree
		if(not this->is_root()) {
			this->parent->add_sample(v);
		}
		
    }
	
	/**
	 * @brief Have all my children been visited?
	 * @return 
	 */	
	bool all_children_visited() const {
		for(auto& c: this->get_children() ) {
			if(c.nvisits == 0) return false;
		}
		return true;
	}
	
	virtual void run_thread(Control ctl, HYP h0) override {
		
		ctl.start();
		
		while(ctl.running()) {
			if(DEBUG_MCTS) DEBUG("\tMCTS SEARCH LOOP");
			
			HYP current = h0; // each thread makes a real copy here 
			this->search_one(current); 
		}		
	}
	

	
	/**
	 * @brief If we can evaluate this current node (usually: compute a posterior and add_sample)
	 * @param current
	 */
	virtual void process_evaluable(HYP& current) {
		open = false; // make sure nobody else takes this one
			
		// if its a terminal, compute the posterior
		current.compute_posterior(*data);
		add_sample(current.likelihood);
		(*callback)(current);
	}

	/**
	 * @brief This gets called before descending the tree if we don't have all of our children. 
	 * 		  NOTE: This could add all the children (default) or be overwritten to add one, or none
	 * @param current
	 */
	virtual void process_add_children(HYP& current) {
		int neigh = current.neighbors(); 
		
		mylock.lock();
		// TODO: This is a bit inefficient because it copies current a bunch of times...
		// this is intentionally a while loop in case another thread has snuck in and added
		while(this->nchildren() < (size_t)neigh) {
			int k = this->nchildren(); // which to expand
			HYP kc = current; kc.expand_to_neighbor(k);
			this->get_children().emplace_back(kc, reinterpret_cast<this_t*>(this), k);
		}		
		mylock.unlock();		
	}

		
	/**
	 * @brief This goes down the tree, sampling children. Defaultly here it's total probability mass per child
	 * @param current
	 */
	virtual void descend(HYP& current) {
		int neigh = current.neighbors(); 
		std::vector<double> children_lps(neigh, -infinity);
		for(int k=0;k<neigh;k++) {
			if(this->child(k).open){
				/// how much probability mass PER sample came from each child, dividing by explore for the temperature.
				/// If no exploraiton steps, we just pretend lse-1.0 was the probability mass 
				children_lps[k] = current.neighbor_prior(k) + 
								  (this->child(k).nvisits == 0 ? lse-1.0 : this->child(k).lse-log(this->child(k).nvisits)) / explore;
			}
		}
		
		// choose an index into children
		int idx = sample_int_lp(neigh, [&](const int i) -> double {return children_lps[i];} ).first;
		
		// expand 
		current.expand_to_neighbor(idx); // idx here gives which expansion we follow
		
		// and recurse down
		this->child(idx).search_one(current);
	}


	/**
	 * @brief recurse down the tree, building and/or evaluating as needed.
	 * @param current
	 */
    void search_one(HYP& current) {
		//  
		
		if(DEBUG_MCTS) DEBUG("MCTS SEARCH ONE ", this, "\t["+current.string()+"] ", nvisits);
		
		++nvisits; // increment our visit count on our way down so other threads don't follow
		
		int neigh = current.neighbors(); 
		
		// if we can evaluate this 
		if(current.is_evaluable()) {
			process_evaluable(current);
		}
		
		// if I don't have all of my children, add them
		if(this->nchildren() != (size_t) neigh) {
			process_add_children(current);
		}
		
		// otherwise keep going down the tree
		if(neigh > 0) {
			descend(current);
		}
		
    } // end search

};

// Must be defined for the linker to find them, apparently:
template<typename this_t, typename HYP, typename callback_t>
double FullMCTSNode<this_t, HYP, callback_t>::explore = 1.0;

template<typename this_t, typename HYP, typename callback_t>
typename HYP::data_t* FullMCTSNode<this_t, HYP,callback_t>::data = nullptr;

template<typename this_t, typename HYP, typename callback_t>
callback_t* FullMCTSNode<this_t, HYP,callback_t>::callback = nullptr;



// A class that calls some number of playouts instead of building the full tree
template<typename this_t, typename HYP, typename callback_t>
class PartialMCTSNode : public FullMCTSNode<this_t,HYP,callback_t> {	
	friend class FullMCTSNode<this_t,HYP,callback_t>;
	using Super = FullMCTSNode<this_t,HYP,callback_t>;
	using Super::Super; // get constructors

	using data_t = typename HYP::data_t;
	static constexpr size_t nplayouts = 100; // when we playout, how many do we do?
	
	/**
	 * @brief Choose the max according to a UCT-like bound
	 * @param current
	 */
	virtual void descend(HYP& current) override {
		
		// check if all my children have been visited
		
		int neigh = current.neighbors(); 
		
		// if everyone has been visited, we'll descend with a UCT-like rule
		if(this->all_children_visited()) {
			std::vector<double> children_lps(neigh, -infinity);		
			for(int k=0;k<neigh;k++) {
				if(this->children[k].open){
					children_lps[k] = current.neighbor_prior(k) + 
									  (this->children[k].nvisits == 0 ? 0.0 : this->children[k].max + explore*sqrt(log(1+this->nvisits)/(1+this->children[k].nvisits)));
				}
			}			
			
			int idx = arg_max_int(neigh, [&](const int i) -> double {return children_lps[i];} ).first;
			current.expand_to_neighbor(idx); // idx here gives which expansion we follow
			this->children[idx].search_one(current);
		}
		else {
			
			// otherwise choose the highest prior one that is unvisited and run playout (which users must define)
			int idx = arg_max_int(neigh, [&](const int k) -> double { return (this->children[k].nvisits == 0 ? current.neighbor_prior(k) : -infinity); } ).first;
			current.expand_to_neighbor(idx); // idx here gives which expansion we follow
			
			// NOTE: here we do not search_one -- we just stop at this node -- adding one expansion to children
			// we have to call children[idx] here because otherwise it won't get the sample
			this->children[idx].nvisits++; // since it's not counted in add_sample and we don't search_one on it
			this->children[idx].playout(current);
		}
		
	}

	/**
	 * @brief This gets called on a child that is unvisited. Typically it would consist of filling in h some number of times and
	 * 		  saving the stats
	 * @param h
	 */
	virtual void playout(HYP& h) {
		for(size_t i=0;i<nplayouts;i++) {
			HYP v = h; v.complete();
			v.compute_posterior(*this->data);
			this->add_sample(v.likelihood);
			(*this->callback)(v);
		}		
	}
	
	
};