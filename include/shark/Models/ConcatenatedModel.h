//===========================================================================
/*!
*  \brief concatenation of two models, with type erasure
*
*  \author  O. Krause
*  \date    2010-2011
*
*  \par Copyright (c) 1999-2011:
*      Institut f&uuml;r Neuroinformatik<BR>
*      Ruhr-Universit&auml;t Bochum<BR>
*      D-44780 Bochum, Germany<BR>
*      Phone: +49-234-32-27974<BR>
*      Fax:   +49-234-32-14209<BR>
*      eMail: Shark-admin@neuroinformatik.ruhr-uni-bochum.de<BR>
*      www:   http://www.neuroinformatik.ruhr-uni-bochum.de<BR>
*
*
*
*  <BR><HR>
*  This file is part of Shark. This library is free software;
*  you can redistribute it and/or modify it under the terms of the
*  GNU General Public License as published by the Free Software
*  Foundation; either version 3, or (at your option) any later version.
*
*  This library is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this library; if not, see <http://www.gnu.org/licenses/>.
*  
*/
//===========================================================================

#ifndef SHARK_MODEL_CONCATENATEDMODEL_H
#define SHARK_MODEL_CONCATENATEDMODEL_H

#include <shark/Models/AbstractModel.h>
#include <shark/LinAlg/BLAS/Initialize.h>

#include <boost/scoped_ptr.hpp>
#include <boost/serialization/scoped_ptr.hpp>

namespace shark {

namespace detail{


///\brief Baseclass for the wrapper which is used to hide the matrix type. 
///
///Additional to the requirement of a Model, a clone() method must be implemented which is used to
///copy a wrapper
template<class InputType, class OutputType>
class ConcatenatedModelWrapperBase:public AbstractModel<InputType,OutputType>{
public:
	virtual ConcatenatedModelWrapperBase<InputType,OutputType>* clone() const = 0;
};

///\brief Internal Wrappertype to connect the output of the first model with the input of the second model.
///
///This model is also created when concatenating two models with operator>> (firstModel>>secondModel)
template<class InputType, class IntermediateType, class OutputType>
class ConcatenatedModelWrapper : public ConcatenatedModelWrapperBase<InputType, OutputType> {
protected:
	typedef typename AbstractModel<InputType,IntermediateType>::BatchOutputType BatchIntermediateType;
	AbstractModel<InputType,IntermediateType>* m_firstModel;
	AbstractModel<IntermediateType,OutputType>* m_secondModel;
	typedef ConcatenatedModelWrapperBase<InputType, OutputType> base_type;

	struct InternalState: public State{
		BatchIntermediateType intermediateResult;
		boost::shared_ptr<State> firstModelState;
		boost::shared_ptr<State> secondModelState;
	};
public:
	typedef typename base_type::BatchInputType BatchInputType;
	
	typedef typename base_type::BatchOutputType BatchOutputType;
	ConcatenatedModelWrapper(
		AbstractModel<InputType, IntermediateType>* firstModel,
		AbstractModel<IntermediateType, OutputType>* secondModel)
	: m_firstModel(firstModel), m_secondModel(secondModel)
	{
		if (firstModel->hasFirstParameterDerivative()
			&& secondModel->hasFirstParameterDerivative()
			&& secondModel ->hasFirstInputDerivative())
		{ 
			this->m_features |= base_type::HAS_FIRST_PARAMETER_DERIVATIVE;
		}

		/*if(firstModel->hasSecondParameterDerivative()
		&& secondModel->hasSecondParameterDerivative()
		&& firstModel ->hasSecondInputDerivative()){
			m_features|=HAS_SECOND_PARAMETER_DERIVATIVE;
		}*/

		if (firstModel->hasFirstInputDerivative()
			&& secondModel->hasFirstInputDerivative())
		{ 
			this->m_features |= base_type::HAS_FIRST_INPUT_DERIVATIVE;
		}

		/*if(firstModel->hasSecondInputDerivative()
		&& secondModel->hasSecondInputDerivative()){
			m_features|=HAS_SECOND_INPUT_DERIVATIVE;
		}*/
		
		this->m_name = "Concatenation<" + firstModel->name() + "," + secondModel->name() + ">";
	}

	ConcatenatedModelWrapperBase<InputType, OutputType>* clone()const{
		return new ConcatenatedModelWrapper<InputType, IntermediateType, OutputType>(*this);
	}

	RealVector parameterVector() const {
		RealVector params(numberOfParameters());
		init(params) << parameters(*m_firstModel), parameters(*m_secondModel);
		return params;
	}

	void setParameterVector(RealVector const& newParameters) {
		init(newParameters) >> parameters(*m_firstModel), parameters(*m_secondModel);
	}
	
	boost::shared_ptr<State> createState()const{
		InternalState* state = new InternalState();
		boost::shared_ptr<State> ptrState(state);
		state->firstModelState = m_firstModel->createState();
		state->secondModelState = m_secondModel->createState();
		return ptrState;
	}

	std::size_t numberOfParameters() const {
		return m_firstModel->numberOfParameters() + m_secondModel->numberOfParameters();
	}

	void eval( BatchInputType const& patterns, BatchOutputType& outputs)const{
		m_secondModel->eval(
			(*m_firstModel)(patterns), 
			outputs
		);
	}
	
	void eval( BatchInputType const& patterns, BatchOutputType& outputs, State& state)const{
		InternalState& s = state.toState<InternalState>();
		m_firstModel->eval(patterns, s.intermediateResult,*s.firstModelState);
		m_secondModel->eval(s.intermediateResult, outputs,*s.secondModelState);
	}

	void weightedParameterDerivative(
		BatchInputType const& patterns, BatchOutputType const& coefficients, State const& state, RealVector& gradient
	)const{
		InternalState const& s = state.toState<InternalState>();
		std::size_t firstParam=m_firstModel->numberOfParameters();
		std::size_t secondParam=m_secondModel->numberOfParameters();
		gradient.resize(firstParam+secondParam);

		RealVector firstParameterDerivative;
		BatchIntermediateType secondInputDerivative;
		RealVector secondParameterDerivative;

		m_secondModel->weightedDerivatives(
			s.intermediateResult,coefficients,*s.secondModelState,
			secondParameterDerivative,secondInputDerivative
		);
		m_firstModel->weightedParameterDerivative(patterns,secondInputDerivative,*s.firstModelState,firstParameterDerivative);

		gradient.resize(firstParam+secondParam);
		init(gradient)<<firstParameterDerivative,secondParameterDerivative;
	}

	void weightedInputDerivative(
		BatchInputType const& patterns, BatchOutputType const& coefficients, State const& state, BatchOutputType& gradient
	)const{
		InternalState const& s = state.toState<InternalState>();
		BatchIntermediateType secondInputDerivative;
		m_secondModel->weightedInputDerivative(s.intermediateResult, coefficients, *s.secondModelState, secondInputDerivative);
		m_firstModel->weightedInputDerivative(patterns, secondInputDerivative, *s.firstModelState, gradient);
	}
	
	//special implementation, because we can reuse the input derivative of the second model for the calculation of both derivatives of the first
	virtual void weightedDerivatives(
		BatchInputType const & patterns, 
		BatchOutputType const & coefficients, 
		State const& state,
		RealVector& parameterDerivative,
		BatchInputType& inputDerivative
	)const{
		InternalState const& s = state.toState<InternalState>();
		std::size_t firstParam=m_firstModel->numberOfParameters();
		std::size_t secondParam=m_secondModel->numberOfParameters();
		parameterDerivative.resize(firstParam+secondParam);

		RealVector firstParameterDerivative;
		BatchIntermediateType secondInputDerivative;
		RealVector secondParameterDerivative;

		m_secondModel->weightedDerivatives(
			s.intermediateResult, coefficients, *s.firstModelState, secondParameterDerivative, secondInputDerivative
		);
		m_firstModel->weightedDerivatives(
			patterns, secondInputDerivative, *s.secondModelState, parameterDerivative, inputDerivative
		);

		parameterDerivative.resize(firstParam+secondParam);
		init(parameterDerivative)<<firstParameterDerivative,secondParameterDerivative;
	}
	/// From ISerializable
	void read( InArchive & archive ){
		m_firstModel->read(archive);
		m_secondModel->read(archive);
	}

	/// From ISerializable
	void write( OutArchive & archive ) const{
		m_firstModel->write(archive);
		m_secondModel->write(archive);
	}
};

///\brief When using operator>> to connect more than two models, this type is created.
///
///When concatenating two models, the ConcatenatedModelWrapper is created. But it is only a temporary object. 
///Thus when concatenating it with another model, it must be made persistent. We do that by simply calling clone() and saving the now
///persistens pointer. Note, that the right-hand-side is not allowed to be a ConcatenatedModelWrapperBase. This is not checked.
template<class InputType, class IntermediateType, class OutputType>
class ConcatenatedModelList:public ConcatenatedModelWrapper<InputType,IntermediateType,OutputType>{
private:
	typedef ConcatenatedModelWrapper<InputType,IntermediateType,OutputType> base_type;
	typedef ConcatenatedModelWrapperBase<InputType,IntermediateType> FirstModelType;
public:	

	ConcatenatedModelList(
		const FirstModelType& firstModel,
		AbstractModel<IntermediateType, OutputType>* secondModel
	):base_type(firstModel.clone(),secondModel){
		if (base_type::m_firstModel->hasFirstParameterDerivative()
			&& secondModel->hasFirstParameterDerivative()
			&& secondModel ->hasFirstInputDerivative())
		{ 
			this->m_features |= base_type::HAS_FIRST_PARAMETER_DERIVATIVE;
		}

		/*if(firstModel->hasSecondParameterDerivative()
		&& secondModel->hasSecondParameterDerivative()
		&& firstModel ->hasSecondInputDerivative()){
			m_features|=HAS_SECOND_PARAMETER_DERIVATIVE;
		}*/

		if (base_type::m_firstModel->hasFirstInputDerivative()
			&& secondModel->hasFirstInputDerivative())
		{ 
			this->m_features |= base_type::HAS_FIRST_INPUT_DERIVATIVE;
		}

		/*if(firstModel->hasSecondInputDerivative()
		&& secondModel->hasSecondInputDerivative()){
			m_features|=HAS_SECOND_INPUT_DERIVATIVE;
		}*/
		
		this->m_name = "Concatenation<" + base_type::m_firstModel->name() + "," + secondModel->name() + ">";
	}
		
	~ConcatenatedModelList(){
		delete base_type::m_firstModel;
	}
	
	ConcatenatedModelWrapperBase<InputType, OutputType>* clone()const{
		return new ConcatenatedModelList<InputType, IntermediateType, OutputType>(
			*static_cast<FirstModelType*>(base_type::m_firstModel),//get the type information back
			base_type::m_secondModel
		);
	}
};

}

///\brief Connects two AbstractModels so that the output of the first model is the input of the second.
///
///The type of the output of the first model must match the type of the input of the second model exactly.
template<class InputT,class IntermediateT,class OutputT>
detail::ConcatenatedModelWrapper<InputT,IntermediateT,OutputT> 
operator>>(AbstractModel<InputT,IntermediateT>& firstModel,AbstractModel<IntermediateT,OutputT>& secondModel){
	return detail::ConcatenatedModelWrapper<InputT,IntermediateT,OutputT> (&firstModel,&secondModel);
}

///\brief Connects another AbstractModel two a previously created connection of models
template<class InputT,class IntermediateT,class OutputT>
detail::ConcatenatedModelList<InputT,IntermediateT,OutputT> 
operator>>(
	const detail::ConcatenatedModelWrapperBase<InputT,IntermediateT>& firstModel,
	AbstractModel<IntermediateT,OutputT>& secondModel
){
	return detail::ConcatenatedModelList<InputT,IntermediateT,OutputT> (firstModel,&secondModel);
}



///\brief ConcatenatedModel concatenates two models such that the output of the first model is input to the second.
///
///Sometimes a series of models is needed to generate the desired output. For example when input data needs to be 
///normalized before it can be put into the trained model. In this case, the ConcatenatedModel can be used to 
///represent this series as one model. 
///The easiest way to do is is using the operator >> of AbstractModel:
///ConcatenatedModel<InputType,OutputType> model = model1>>model2;
///InputType must be the type of input model1 receives and model2 the output of model2. The output of model1 and input
///of model2 must match. Another way of construction is calling the constructor of ConcatenatedModel using the constructor:
/// ConcatenatedModel<InputType,OutputType> model (&modell,&model2);
///warning: model1 and model2 must outlive model. When they are destroyed first, behavior is undefined.
template<class InputType, class OutputType>
class ConcatenatedModel: public AbstractModel<InputType,OutputType> {
protected:
	boost::scoped_ptr<detail::ConcatenatedModelWrapperBase<InputType, OutputType> > m_wrapper;
	typedef AbstractModel<InputType, OutputType> base_type;

public:
	typedef typename base_type::BatchInputType BatchInputType;
	typedef typename base_type::BatchOutputType BatchOutputType;


	///creates a concatenated model using two base model. this is equivalent to concModel = *firstModel >> *secondModel;
	template<class T>
	ConcatenatedModel(AbstractModel<InputType, T>* firstModel, AbstractModel<T, OutputType>* secondModel) {
		m_wrapper.reset(new detail::ConcatenatedModelWrapper<InputType, T, OutputType>(firstModel, secondModel));
		if (m_wrapper->hasFirstParameterDerivative()){ 
			this->m_features |= base_type::HAS_FIRST_PARAMETER_DERIVATIVE; 
		}

		if (m_wrapper->hasFirstInputDerivative()){ 
			this->m_features |= base_type::HAS_FIRST_INPUT_DERIVATIVE; 
		}

		this->m_name = m_wrapper->name();
	}
	///copy constructor to allow ConcatenatedModel concModel = model1 >> model2 >> model3;
	ConcatenatedModel(const detail::ConcatenatedModelWrapperBase<InputType,OutputType>& wrapper) {
		m_wrapper.reset(wrapper.clone());
		if (m_wrapper->hasFirstParameterDerivative()){ 
			this->m_features |= base_type::HAS_FIRST_PARAMETER_DERIVATIVE; 
		}

		if (m_wrapper->hasFirstInputDerivative()){ 
			this->m_features |= base_type::HAS_FIRST_INPUT_DERIVATIVE; 
		}

		this->m_name = m_wrapper->name();
	}
	///operator =  to allow concModel = model1 >> model2 >> model3; for a previously declared concatenadel model
	ConcatenatedModel<InputType,OutputType>& operator = ( detail::ConcatenatedModelWrapperBase<InputType,OutputType>& wrapper ){
		m_wrapper.reset(wrapper.clone());
		if (m_wrapper->hasFirstParameterDerivative()){ 
			this->m_features |= base_type::HAS_FIRST_PARAMETER_DERIVATIVE; 
		}

		if (m_wrapper->hasFirstInputDerivative()){ 
			this->m_features |= base_type::HAS_FIRST_INPUT_DERIVATIVE; 
		}

		this->m_name = m_wrapper->name();
		return *this;
	}

	ConcatenatedModel(const ConcatenatedModel<InputType, OutputType>& src)
	:m_wrapper(src.m_wrapper->clone()) {
		this->m_name = src.m_name;
		this->m_features = src.m_features;
	}

	const ConcatenatedModel<InputType,OutputType>& operator = (const ConcatenatedModel<InputType, OutputType>& src) {
		ConcatenatedModel<InputType,OutputType> copy(src);
		swap(m_wrapper,copy.m_wrapper);
		swap(base_type::m_name,copy.m_name);
		std::swap(base_type::m_features,copy.m_features);
		return *this;
	}

	RealVector parameterVector() const {
		return m_wrapper->parameterVector();
	}

	void setParameterVector(RealVector const& newParameters) {
		m_wrapper->setParameterVector(newParameters);
	}

	size_t numberOfParameters() const {
		return m_wrapper->numberOfParameters();
	}
	
	boost::shared_ptr<State> createState()const{
		return m_wrapper->createState();
	}

	using base_type::eval;
	void eval(BatchInputType const& patterns, BatchOutputType& outputs)const {
		m_wrapper->eval(patterns, outputs);
	}
	void eval(BatchInputType const& patterns, BatchOutputType& outputs, State& state)const {
		m_wrapper->eval(patterns, outputs, state);
	}
	
	void weightedParameterDerivative(
		BatchInputType const& patterns, BatchOutputType const& coefficients, State const& state, RealVector& gradient
	)const{
		m_wrapper->weightedParameterDerivative(patterns, coefficients, state, gradient);
	}

	void weightedInputDerivative(
		BatchInputType const& patterns, BatchOutputType const& coefficients, State const& state, BatchOutputType& derivatives
	)const{
		m_wrapper->weightedInputDerivative(patterns, coefficients, state, derivatives);
	}

	virtual void weightedDerivatives(
		BatchInputType const & patterns,
		BatchOutputType const & coefficients,
		State const& state,
		RealVector& parameterDerivative,
		BatchInputType& inputDerivative
	)const{
		m_wrapper->weightedDerivatives(patterns, coefficients, state, parameterDerivative,inputDerivative);
	}

	/// From ISerializable
	void read( InArchive & archive ){
		m_wrapper->read(archive);
	}

	/// From ISerializable
	void write( OutArchive & archive ) const{
		m_wrapper->write(archive);
	}
};


}
#endif