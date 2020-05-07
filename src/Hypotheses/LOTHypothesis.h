#pragma once


#include <string.h>
#include "Proposers.h"
#include "Program.h"
#include "Node.h"
#include "DiscreteDistribution.h"

#include "Hypotheses/Interfaces/Bayesable.h"
#include "Hypotheses/Interfaces/MCMCable.h"
#include "Hypotheses/Interfaces/Searchable.h"

#include "VirtualMachine/VirtualMachineState.h"

/**
 * @class LOTHypothesis
 * @author piantado
 * @date 05/05/20
 * @file LOTHypothesis.h
 * @brief A LOTHypothesis is the basic unit for doing LOT models. It is templated with itself (the curiously recurring tempalte
 *        pattern), an input type, and output type, a grammar type, and types for the individual data elements and vector
 *        of data. Usually you will subclass this (or a Lexicon) as hypotheses in a LOT model. 
 * 		  
 * 		  The kind of virtual machinse that are called here are defined inside Grammar (even though LOTHypothesis would be a 
 *        more natural palce) because we need access to the Grammar's parameter pack over types. 
 */
template<typename HYP, 
		 typename input_t, 
		 typename output_t, 
		 typename _Grammar_t,
		 typename _datum_t=defauldatum_t<input_t, output_t>, 
		 typename _data_t=std::vector<_datum_t>,
		 typename VirtualMachineState_t=typename _Grammar_t::template VirtualMachineState_t<input_t, output_t> // used for deducing VM_TYPES in VirtualMachineState
		 >
class LOTHypothesis : public ProgramLoader,
				      public MCMCable<HYP,_datum_t,_data_t>, // remember, this defines data_t, datum_t
					  public Searchable<HYP,input_t,output_t>	{
public:     
	typedef typename Bayesable<_datum_t,_data_t>::datum_t datum_t;
	typedef typename Bayesable<_datum_t,_data_t>::data_t   data_t;
	using Grammar_t = _Grammar_t;
	
	static const size_t MAX_NODES = 64; // max number of nodes we allow; otherwise -inf prior
	
	Grammar_t* grammar;
	
	Node value;

	LOTHypothesis(Grammar_t* g=nullptr)     : MCMCable<HYP,datum_t,data_t>(), grammar(g), value(NullRule,0.0,true) {}
	LOTHypothesis(Grammar_t* g, Node&& x)   : MCMCable<HYP,datum_t,data_t>(), grammar(g), value(x) {}
	LOTHypothesis(Grammar_t* g, Node& x)    : MCMCable<HYP,datum_t,data_t>(), grammar(g), value(x) {}

	// parse this from a string
	LOTHypothesis(Grammar_t* g, std::string s) : MCMCable<HYP,datum_t,data_t>(), grammar(g)  {
		value = grammar->expand_from_names(s);
	}
	
	
	[[nodiscard]] virtual std::pair<HYP,double> propose() const override {
		/**
		 * @brief Default proposal is rational-rules style regeneration. 
		 * @return 
		 */
	
		assert(grammar != nullptr);

		// simplest way of doing proposals
		auto x = Proposals::regenerate(grammar, value);	
		
		// return a pair of hypothesis and forward-backward probabilities
		return std::make_pair(HYP(this->grammar, std::move(x.first)), x.second); // return HYP and fb
	}	

	
	[[nodiscard]] virtual HYP restart() const override {
		/**
		 * @brief This is used to restart chains, sampling from prior
		 * @return 
		 */
		
		assert(grammar != nullptr);
		
		// This is used in MCMC to restart chains 
		// this ordinarily would be a resample from the grammar, but sometimes we have can_resample=false
		// and in that case we want to leave the non-propose nodes alone. 

		if(!value.is_null()) { // if we are null
			return HYP(this->grammar, this->grammar->copy_resample(value, [](const Node& n) { return n.can_resample; }));
		}
		else {
			return HYP(this->grammar, this->grammar->template generate<output_t>());
		}
	}
	
	void set_value(Node&  v) { value = v; }
	void set_value(Node&& v) { value = v; }
	
	virtual double compute_prior() override {
		assert(grammar != nullptr && "Grammar was not initialized before trying to call compute_prior");
		
		/* This ends up being a really important check -- otherwise we spend tons of time on really long
		 * hypotheses */
		if(this->value.count() > MAX_NODES) {
			return this->prior = -infinity;
		}
		
		return this->prior = grammar->log_probability(value);
	}
	
	virtual double compute_single_likelihood(const datum_t& datum) override {
		// compute the likelihood of a *single* data point. 
		assert(0);// for base classes to implement, but don't set = 0 since then we can't create Hypothesis classes. 
	}

	virtual void push_program(Program& s, short k=0) override {
		assert(k==0); // this is only for lexica
		s.reserve(128); // seems to help to reserve some
		value.linearize(s);
	}


	// we defaultly map outputs to log probabilities
	// the HYP must be a ProgramLoader, but other than that we don't care. NOte that this type gets passed all the way down to VirtualMachine
	// and potentially back to primitives, allowing us to access the current hypothesis if we want
	// LOADERHYP is the kind of hypothesis we use to load, and it is not the same as HYP
	// because in a Lexicon, we want to use its InnerHypothesis
	template<typename LOADERHYP> 
	DiscreteDistribution<output_t> call(const input_t x, const output_t err, LOADERHYP* loader, 
				unsigned long max_steps=2048, unsigned long max_outputs=256, double minlp=-10.0){
		
		VirtualMachineState_t* vms = new VirtualMachineState_t(x, err);	
		push_program(vms->opstack); // write my program into vms (loader is used for everything else)

		VirtualMachinePool pool(vms, max_steps, max_outputs, minlp); // vms is passed here just to deduce the type
		
		return pool.template run(loader);		
	}
	virtual DiscreteDistribution<output_t> call(const input_t x, const output_t err) {
		return call(x, err, this); // defaultly I myself am the recursion handler and dispatch
	}
	auto operator()(const input_t x, const output_t err){ // just fancy syntax for call
		return call(x,err);
	}

	template<typename LOADERHYP>
	output_t callOne(const input_t x, const output_t err, LOADERHYP* loader) {
		// we can use this if we are guaranteed that we don't have a stochastic hypothesis
		// the savings is that we don't have to create a VirtualMachinePool		
		VirtualMachineState_t vms(x, err);		

		push_program(vms.opstack); // write my program into vms (loader is used for everything else)
		return vms.run(loader); // default to using "this" as the loader		
	}
	
	output_t callOne(const input_t x, const output_t err) {
		// we can use this if we are guaranteed that we don't have a stochastic hypothesis
		// the savings is that we don't have to create a VirtualMachinePool		
		VirtualMachineState_t vms(x, err);		

		push_program(vms.opstack); // write my program into vms (loader is used for everything else)
		return vms.run(this); // default to using "this" as the loader		
	}
	

	virtual std::string string() const override {
		return std::string("\u03BBx.") + value.string();
	}
	virtual std::string parseable() const { 
		return value.parseable(); 
	}
	static HYP from_string(Grammar_t& g, std::string s) {
		return HYP(g, g.expand_from_names(s));
	}
	
	
	virtual size_t hash() const override {
		return value.hash();
	}
	
	virtual bool operator==(const HYP& h) const override {
		return this->value == h.value;
	}
	
	virtual HYP copy_and_complete() const {
		// make a copy and fill in the missing nodes.
		// NOTE: here we set all of the above nodes to NOT resample
		// TODO: That part should go somewhere else eventually I think?
		HYP h(grammar, Node(value));
		h.prior=0.0;h.likelihood=0.0;h.posterior=0.0; // reset these just in case
		
		const std::function<void(Node&)> myf =  [](Node& n){n.can_resample=false;};
		h.value.map(myf);
		grammar->complete(h.value);

		return h;
	}

	
	/********************************************************
	 * Implementation of Searchable interace 
	 ********************************************************/
	 // The main complication with these is that they handle nullptr 
	 
	virtual int neighbors() const override {
		if(value.is_null()) { // if the value is null, our neighbors is the number of ways we can do nt
			auto nt = grammar->template nt<output_t>();
			return grammar->count_rules(nt);
		}
		else {
			return grammar->neighbors(value);
//			 to rein in the mcts branching factor, we'll count neighbors as just the first unfilled gap
//			 we should not need to change make_neighbor since it fills in the first, first
//			return value.first_neighbors(*grammar);
		}
	}

	virtual HYP make_neighbor(int k) const override {
		assert(grammar != nullptr);
		
		HYP h(grammar); // new hypothesis
		auto nt = grammar->template nt<output_t>();
		if(value.is_null()) {
			assert(k >= 0);
			assert(k < (int)grammar->count_rules(nt));
			auto r = grammar->get_rule(nt,(size_t)k);
			h.value = grammar->makeNode(r);
		}
		else {
			Node t = value;
			grammar->expand_to_neighbor(t,k);
			h.value = t;
		}
		return h;
	}
	virtual bool is_evaluable() const override {
		// This checks whether it should be allowed to call "call" on this hypothesis. 
		// Usually this means that that the value is complete, meaning no partial subtrees
		return value.is_complete();
	}
	 
};
