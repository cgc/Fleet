#pragma once 

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define hypothesis
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include "ConstantContainer.h"
#include "DeterministicLOTHypothesis.h"
#include "Random.h"


// we also consider our scale variables (proposals to constants) as powers of 10
const int MIN_SCALE = -3; 
const int MAX_SCALE = 4;

// most nodes we'll consider in a hypothesis
const size_t MY_MAX_NODES = 35;

// we use at most this many constants; note that if we have higher, we are given zero prior. 
// this value kinda matters because we assume we always have this many, in order to avoid problems
// with changing dimensionality.  
const size_t N_CONSTANTS = 4; 

// scale for the prior on constants
const double LAPLACE_SCALE = 1.0;


class MyHypothesis final : public ConstantContainer,
						   public DeterministicLOTHypothesis<MyHypothesis,X_t,D,MyGrammar,&grammar> {
	
public:

	using Super = DeterministicLOTHypothesis<MyHypothesis,X_t,D,MyGrammar,&grammar>;
	using Super::Super;

	virtual D call(const X_t x, const D err=NaN) {
		// We need to override this because DeterministicLOTHypothesis::call asserts that the program is non-empty
		// but actually ours can be if we are only a constant. 
		// my own wrapper that zeros the constant_i counter
		
		constant_idx = 0;
		try { 
			const auto out = Super::call(x,err);
			if(constant_idx != count_constants()) {
				PRINTN(string());
			}
			assert(constant_idx == count_constants()); // just check we used all constants
			return out;
		}
		catch(TooManyConstantsException& e) {
			return err;
		}
	}
	
	virtual double constant_prior() const {
		// we're going to override and do a LSE over scales 
		
		if(count_constants() > N_CONSTANTS) {
			return -infinity;
		}
		
		double lp = 0.0;
		for(auto& c : constants) {
			lp += laplace_lpdf(c, 0., LAPLACE_SCALE);
		} 
		return lp;
	}
	
	virtual void randomize_constants() override {
		// NOTE: this MUST match how the prior is computed
		constants.resize(N_CONSTANTS);
		for(size_t i=0;i<N_CONSTANTS;i++) {		
			constants[i] = random_laplace(0.0, LAPLACE_SCALE);
		}
	}
	
	size_t count_constants() const override {
		// below is if we actually want to count
		size_t cnt = 0;
		for(const auto& x : value) {
			cnt += isConstant(x.rule);
		}
		return cnt;
	}
	
	// Propose to a constant c, returning a new value and fb
	// NOTE: When we use a symmetric drift kernel, fb=0
	std::pair<double,double> constant_proposal(double c) const override { 
			
		if(flip(0.90)) {
			auto sc = pow(10, myrandom(MIN_SCALE, MAX_SCALE)); // pick a scale here 
			
			return std::make_pair(random_normal(c,  sc), 0.0);
		}
		else if(flip(0.5)) { 
			// one third probability for each of the other choices
			auto v = random_normal(data_X_mean, data_X_sd);
			double fb = normal_lpdf(v, data_X_mean, data_X_sd) - 
						normal_lpdf(c, data_X_mean, data_X_sd);
			return std::make_pair(v,fb);
		}
		else {
			auto v = random_normal(data_Y_mean, data_Y_sd);
			double fb = normal_lpdf(v, data_Y_mean, data_Y_sd) - 
						normal_lpdf(c, data_Y_mean, data_Y_sd);
			return std::make_pair(v,fb);
		}
	}

	double compute_single_likelihood(const datum_t& datum) override {
		double fx = this->call(datum.input, NaN);
		
		if(std::isnan(fx) or std::isinf(fx)) 
			return -infinity;
			
		//PRINTN(string(), datum.output, fx, datum.reliability, normal_lpdf( fx, datum.output, datum.reliability ));
		
		return normal_lpdf(fx, datum.output, datum.reliability );		
	}
	
	virtual double compute_prior() override {
		
		if(this->value.count() > MY_MAX_NODES) {
			return this->prior = -infinity;
		}
		
		// check to see if it uses all variables. 
		#ifdef REQUIRE_USE_ALL_VARIABLES
		std::vector<bool> uses_variable(NUM_VARS, false);
		for(const auto& n : this->value) {
			if(n.rule->format[0] == 'x') {
				std::string s = n.rule->format; s.erase(0,1); // remove x
				uses_variable[string_to<int>(s)] = true;
			}
		}
		for(const auto& v : uses_variable) { // check that we used everything
			if(not v) return this->prior = -infinity; 
		}
		#endif
		
		this->prior = Super::compute_prior() + this->constant_prior();
		return this->prior;
	}
	
	virtual std::string __my_string_recurse(const Node* n, size_t& idx) const {
		// we need this to print strings -- its in a similar format to evaluation
		if(isConstant(n->rule)) {
			return "("+to_string_with_precision(constants[idx++], 14)+")";
		}
		else if(n->rule->N == 0) {
			return n->rule->format;
		}
		else {
			
			// strings are evaluated in right->left order so we have to 
			// use that here (since we use them to index idx)
			std::vector<std::string> childStrings(n->nchildren());
			
			/// recurse on the children. NOTE: they are linearized left->right, 
			// which means that they are popped 
			for(size_t i=0;i<n->rule->N;i++) {
				childStrings[i] = __my_string_recurse(&n->child(i),idx);
			}
			
			std::string s = n->rule->format;
			for(size_t i=0;i<n->rule->N;i++) { // can't be size_t for counting down
				auto pos = s.find(Rule::ChildStr);
				assert(pos != std::string::npos); // must contain the ChildStr for all children all children
				s.replace(pos, Rule::ChildStr.length(), childStrings[i]);
			}
			
			return s;
		}
	}
	
	virtual std::string string(std::string prefix="") const override { 
		// we can get here where our constants have not been defined it seems...
		if(not this->is_evaluable()) 
			return structure_string(); // don't fill in constants if we aren't complete
		
		size_t idx = 0;
		return  prefix + LAMBDAXDOT_STRING +  __my_string_recurse(&value, idx);
	}
	
	virtual std::string structure_string(bool usedot=true) const {
		return Super::string("", usedot);
	}
	
	/// *****************************************************************************
	/// Change equality to include equality of constants
	/// *****************************************************************************
	
	virtual bool operator==(const MyHypothesis& h) const override {
		// equality requires our constants to be equal 
		return this->Super::operator==(h) and ConstantContainer::operator==(h);
	}

	virtual size_t hash() const override {
		// hash includes constants so they are only ever equal if constants are equal
		size_t h = Super::hash();
		hash_combine(h, ConstantContainer::hash());
		return h;
	}
	
	/// *****************************************************************************
	/// Implement MCMC moves as changes to constants
	/// *****************************************************************************
	
	virtual ProposalType propose() const override {
		// Our proposals will either be to constants, or entirely from the prior
		// Note that if we have no constants, we will always do prior proposals
		
		if(flip(0.85)){
			MyHypothesis ret = *this;
			
			double fb = 0.0; 
			
			// ensure we sample at least one
			std::vector<bool> should_propose(N_CONSTANTS, false);
			for(size_t i=0;i<N_CONSTANTS;i++) {
				should_propose[i] = flip(0.1);
			}
			should_propose[myrandom(N_CONSTANTS)] = true; // always ensure one
			
			// now add to all that I have
			for(size_t i=0;i<N_CONSTANTS;i++) {  // note N_CONSTANTS here, so we propose to the whole vector
				if(should_propose[i]) {
					auto [v, __fb] = this->constant_proposal(constants[i]);
					ret.constants[i] = v;
					fb += __fb;
				}
			}
			
			return std::make_pair(ret, fb);
		}
		else {
			
			ProposalType p; 
			
			if(flip(0.5))       p = Proposals::regenerate(&grammar, value);	
			else if(flip(0.1))  p = Proposals::sample_function_leaving_args(&grammar, value);
			else if(flip(0.1))  p = Proposals::swap_args(&grammar, value);
			else if(flip())     p = Proposals::insert_tree(&grammar, value);	
			else                p = Proposals::delete_tree(&grammar, value);			
			
			if(not p) return {};
			auto x = p.value();
			
			MyHypothesis ret{std::move(x.first)};
			ret.randomize_constants(); // with random constants -- this resizes so that it's right for propose
			
			double fb = x.second + ret.constant_prior()-this->constant_prior();
						
			return std::make_pair(ret, fb); 
		}
			
	}
		
	virtual MyHypothesis restart() const override {
		MyHypothesis ret = Super::restart(); // reset my structure
		ret.randomize_constants();
		return ret;
	}
	
	virtual void complete() override {
		Super::complete();
		randomize_constants();
	}
	
	virtual MyHypothesis make_neighbor(int k) const override {
		auto ret = Super::make_neighbor(k);
		ret.randomize_constants();
		return ret;
	}
	virtual void expand_to_neighbor(int k) override {
		Super::expand_to_neighbor(k);
		randomize_constants();		
	}
	
	[[nodiscard]] static MyHypothesis sample() {
		auto ret = Super::sample();
		ret.randomize_constants();
		return ret;
	}
};

