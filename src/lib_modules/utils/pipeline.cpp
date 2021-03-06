#include "pipeline.hpp"
#include "stranded_pool_executor.hpp"
#include <typeinfo>
#include "helper.hpp"

#define EXECUTOR_SYNC         Signals::ExecutorSync<void()>
#define EXECUTOR_ASYNC_THREAD Signals::ExecutorThread<void()>
#define EXECUTOR_ASYNC_POOL   StrandedPoolModuleExecutor
#define EXECUTOR EXECUTOR_ASYNC_POOL

using namespace Modules;

namespace Pipelines {

template<typename Class>
Signals::MemberFunctor<void, Class, void(Class::*)()>
MEMBER_FUNCTOR_NOTIFY_FINISHED(Class* objectPtr) {
	return Signals::MemberFunctor<void, Class, void(Class::*)()>(objectPtr, &ICompletionNotifier::finished);
}

class PipelinedInput : public IInput {
	public:
		PipelinedInput(IInput *input, Modules::IProcessExecutor &executor, ICompletionNotifier * const notify)
			: delegate(input), notify(notify), executor(executor) {}
		virtual ~PipelinedInput() noexcept(false) {}

		/* direct call: receiving nullptr stops the execution */
		virtual void process() override {
			auto data = pop();
			if (data) {
				Log::msg(Debug, format("Module %s: dispatch data for time %s", typeid(delegate).name(), data->getTime() / (double)IClock::Rate));
				delegate->push(data);
				executor(MEMBER_FUNCTOR_PROCESS(delegate));
			} else {
				Log::msg(Debug, format("Module %s: notify finished.", typeid(delegate).name()));
				executor(MEMBER_FUNCTOR_NOTIFY_FINISHED(notify));
			}
		}

		virtual size_t getNumConnections() const override {
			return delegate->getNumConnections();
		}
		virtual void connect() override {
			delegate->connect();
		}

	private:
		IInput *delegate;
		ICompletionNotifier * const notify;
		Modules::IProcessExecutor &executor;
};

class PipelinedModule : public ICompletionNotifier, public IPipelinedModule, public Modules::InputCap {
public:
	/* take ownership of module */
	PipelinedModule(Modules::Module *module, ICompletionNotifier *notify);
	~PipelinedModule() noexcept(false) {}

	size_t getNumInputs() const override;
	size_t getNumOutputs() const override;
	Modules::IOutput* getOutput(size_t i) const override;

	/* source modules are stopped manually - then the message propagates to other connected modules */
	bool isSource() const override;
	bool isSink() const override;

private:
	virtual void connect(Modules::IOutput *output, size_t inputIdx) override;
	Modules::IInput* getInput(size_t i) override;
	void mimicInputs();

	/* uses the executor (i.e. may defer the call) */
	void process() override;
	void finished() override;

	std::unique_ptr<Modules::Module> delegate;
	std::unique_ptr<Modules::IProcessExecutor> const localExecutor;
	Modules::IProcessExecutor &executor;
	ICompletionNotifier* const m_notify;
};

PipelinedModule::PipelinedModule(Module *module, ICompletionNotifier *notify)
	: delegate(module), localExecutor(new EXECUTOR), executor(*localExecutor), m_notify(notify) {
}

void PipelinedModule::mimicInputs() {
	auto const delegateInputs = delegate->getNumInputs();
	auto const thisInputs = inputs.size();
	if (thisInputs < delegateInputs) {
		for (size_t i = thisInputs; i < delegateInputs; ++i) {
			addInput(new PipelinedInput(delegate->getInput(i), this->executor, this));
		}
	}
}

size_t PipelinedModule::getNumInputs() const {
	return delegate->getNumInputs();
}

IInput* PipelinedModule::getInput(size_t i) {
	mimicInputs();
	if (i >= inputs.size())
		throw std::runtime_error(format("PipelinedModule %s: no input %s.", typeid(delegate).name(), i));
	return inputs[i].get();
}

size_t PipelinedModule::getNumOutputs() const {
	return delegate->getNumOutputs();
}

IOutput* PipelinedModule::getOutput(size_t i) const {
	if (i >= delegate->getNumOutputs())
		throw std::runtime_error(format("PipelinedModule %s: no output %s.", typeid(delegate).name(), i));
	return delegate->getOutput(i);
}

bool PipelinedModule::isSource() const {
	if (delegate->getNumInputs() == 0) {
		return true;
	} else if (delegate->getNumInputs() == 1 && dynamic_cast<Modules::Input<DataLoose, Modules::IProcessor>*>(delegate->getInput(0))) {
		return true;
	} else {
		return false;
	}
}

bool PipelinedModule::isSink() const {
	return delegate->getNumOutputs() == 0;
}

void PipelinedModule::connect(IOutput *output, size_t inputIdx) {
	ConnectOutputToInput(output, getInput(inputIdx), &g_executorSync);
}

void PipelinedModule::process() {
	Log::msg(Debug, format("Module %s: dispatch data", typeid(delegate).name()));

	if (isSource()) {
		if (getNumInputs() == 0) {
			/*first time: create a fake pin and push null to trigger execution*/
			delegate->addInput(new Input<DataLoose>(delegate.get()));
			getInput(0)->push(nullptr);
			delegate->getInput(0)->push(nullptr);
			executor(MEMBER_FUNCTOR_PROCESS(delegate.get()));
			executor(MEMBER_FUNCTOR_PROCESS(getInput(0)));
			return;
		} else {
			/*the source is likely processing: push null in the loop to exit and let things follow their way*/
			delegate->getInput(0)->push(nullptr);
			return;
		}
	}

	Data data = getInput(0)->pop();
	for (size_t i = 0; i < getNumInputs(); ++i) {
		getInput(i)->push(data);
		getInput(i)->process();
	}
}

/* end of stream */
void PipelinedModule::finished() {
	delegate->flush();
	if (isSink()) {
		m_notify->finished();
	} else {
		for (size_t i = 0; i < delegate->getNumOutputs(); ++i) {
			delegate->getOutput(i)->emit(nullptr);
		}
	}
}

Pipeline::Pipeline(bool isLowLatency) : isLowLatency(isLowLatency), numRemainingNotifications(0) {
}

IModule* Pipeline::addModule(Module *rawModule) {
	if (!rawModule)
		return nullptr;
	rawModule->setLowLatency(isLowLatency);
	auto module = uptr(new PipelinedModule(rawModule, this));
	auto ret = module.get();
	modules.push_back(std::move(module));
	return ret;
}

void Pipeline::connect(IModule *prev, size_t outputIdx, IModule *n, size_t inputIdx) {
	auto next = safe_cast<IPipelinedModule>(n);
	if (next->isSink())
		numRemainingNotifications++;
	next->connect(prev->getOutput(outputIdx), inputIdx);
}

void Pipeline::start() {
	Log::msg(Info, "Pipeline: starting");
	for (auto &m : modules) {
		if (m->isSource())
			m->process();
	}
	Log::msg(Info, "Pipeline: started");
}

void Pipeline::waitForCompletion() {
	Log::msg(Info, "Pipeline: waiting for completion (remaning: %s)", (int)numRemainingNotifications);
	std::unique_lock<std::mutex> lock(mutex);
	while (numRemainingNotifications > 0) {
		condition.wait(lock);
	}
	Log::msg(Info, "Pipeline: completed");
}

void Pipeline::exitSync() {
	Log::msg(Warning, format("Pipeline: asked to exit now."));
	for (auto &m : modules) {
		if (m->isSource())
			m->process();
	}
}

void Pipeline::finished() {
	std::unique_lock<std::mutex> lock(mutex);
	assert(numRemainingNotifications > 0);
	--numRemainingNotifications;
	condition.notify_one();
}

}
