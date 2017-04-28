﻿using System.Collections.ObjectModel;
using Caliburn.Micro;
using System.Collections.Generic;
using System;
using System.Diagnostics;
using System.Threading.Tasks;
using System.Linq;
using System.Threading;
using System.Globalization;
using System.Xml;
using Herd;
using Badger.Data;


namespace Badger.ViewModels
{
    public class ExperimentBatch
    {
        private List<MonitoredExperimentViewModel> m_monitoredExperiments;

        private HerdAgentViewModel m_herdAgent;
        public HerdAgentViewModel herdAgent { get { return m_herdAgent; } }
        private string m_name;
        public CancellationToken m_cancelToken;
        private Shepherd m_shepherd;

        private PlotViewModel m_evaluationPlot;
        private Dictionary<string, int> m_experimentSeriesId;

        private List<MonitoredExperimentViewModel> m_failedExperiments = new List<MonitoredExperimentViewModel>();
        public List<MonitoredExperimentViewModel> failedExperiments { get { return m_failedExperiments; } set { } }

        private Logger.LogFunction m_logFunction = null;
        private void logMessage(string message)
        {
            m_logFunction?.Invoke(message);
        }

        public ExperimentBatch(string name, List<MonitoredExperimentViewModel> experiments, HerdAgentViewModel herdAgent,
            PlotViewModel evaluationPlot, CancellationToken cancelToken, Logger.LogFunction logFunction)
        {
            m_name = name;
            m_monitoredExperiments = experiments;
            m_herdAgent = herdAgent;
            m_logFunction = logFunction;
            m_shepherd = new Shepherd();
            m_shepherd.setLogMessageHandler(logFunction);
            m_cancelToken = cancelToken;
            m_evaluationPlot = evaluationPlot;
            m_experimentSeriesId = new Dictionary<string, int>();
        }

        private CJob getJob()
        {
            CJob job = new CJob();
            job.name = m_name;

            // tasks, inputs and outputs
            foreach (MonitoredExperimentViewModel experiment in m_monitoredExperiments)
            {
                CTask task = new CTask();
                // We are assuming the same exe file is used in all the experiments!!!
                // IMPORTANT

                task.name = experiment.Name;
                task.exe = experiment.ExeFile;
                task.arguments = experiment.FilePath + " -pipe=" + experiment.PipeName;
                task.pipe = experiment.PipeName;

                job.tasks.Add(task);
                // add EXE files

                if (!job.inputFiles.Contains(task.exe))
                    job.inputFiles.Add(task.exe);
                //add prerrequisites
                foreach (string pre in experiment.Prerequisites)
                    if (!job.inputFiles.Contains(pre))
                        job.inputFiles.Add(pre);

                //add experiment file to inputs
                if (!job.inputFiles.Contains(experiment.FilePath))
                    job.inputFiles.Add(experiment.FilePath);

                Utility.getInputsAndOutputs(experiment.ExeFile, experiment.FilePath, ref job);
            }
            return job;
        }

        public async Task<ExperimentBatch> sendJobAndMonitor()
        {
            m_failedExperiments.Clear();
            try
            {
                //SEND THE JOB DATA
                m_monitoredExperiments.ForEach((exp) => exp.state = MonitoredExperimentViewModel.ExperimentState.WAITING_EXECUTION);
                CJob job = getJob();

                bool bConnected = m_shepherd.connectToHerdAgent(m_herdAgent.ipAddress);
                if (bConnected)
                {
                    logMessage("Sending job to herd agent " + m_herdAgent.ipAddress);
                    m_monitoredExperiments.ForEach((exp) => exp.state = MonitoredExperimentViewModel.ExperimentState.SENDING);
                    m_herdAgent.status = "Sending job query";
                    m_shepherd.SendJobQuery(job, m_cancelToken);
                    logMessage("Job sent to herd agent " + m_herdAgent.ipAddress);
                    //await m_shepherd.waitAsyncWriteOpsToFinish();
                    m_monitoredExperiments.ForEach((exp) => exp.state = MonitoredExperimentViewModel.ExperimentState.RUNNING);
                    m_herdAgent.status = "Executing job query";
                }
                else
                {
                    foreach (MonitoredExperimentViewModel exp in m_monitoredExperiments) m_failedExperiments.Add(exp);
                    m_monitoredExperiments.ForEach((exp) => exp.state = MonitoredExperimentViewModel.ExperimentState.ERROR);
                    logMessage("Failed to connect to herd agent " + m_herdAgent.ipAddress);
                    return this;
                }
                logMessage("Monitoring remote job run by herd agent " + m_herdAgent.ipAddress);
                //MONITOR THE REMOTE JOB
                while (true)
                {
                    int numBytesRead = await m_shepherd.readAsync(m_cancelToken);
                    m_cancelToken.ThrowIfCancellationRequested();

                    string xmlItem = m_shepherd.m_xmlStream.processNextXMLItem();

                    while (xmlItem != "")
                    {
                        string experimentId = m_shepherd.m_xmlStream.getLastXMLItemTag();
                        string message = m_shepherd.m_xmlStream.getLastXMLItemContent();
                        MonitoredExperimentViewModel experimentVM = m_monitoredExperiments.Find(exp => exp.Name == experimentId);
                        string messageId = m_shepherd.m_xmlStream.getLastXMLItemTag(); //previous call to getLastXMLItemContent reset lastXMLItem
                        string messageContent = m_shepherd.m_xmlStream.getLastXMLItemContent();
                        if (experimentVM != null)
                        {
                            if (messageId == "Progress")
                            {
                                double progress = double.Parse(messageContent, CultureInfo.InvariantCulture);
                                experimentVM.progress = Convert.ToInt32(progress);
                            }
                            else if (messageId == "Evaluation")
                            {
                                //<Evaluation>0.0,-1.23</Evaluation>
                                string[] values = messageContent.Split(',');
                                string seriesName = experimentVM.Name;
                                int seriesId;
                                if (values.Length == 2)
                                {
                                    if (!m_experimentSeriesId.Keys.Contains(experimentVM.Name))
                                    {
                                        seriesId = m_evaluationPlot.addLineSeries(seriesName);
                                        m_experimentSeriesId.Add(seriesName, seriesId);
                                    }
                                    else seriesId = m_experimentSeriesId[seriesName];

                                    m_evaluationPlot.addLineSeriesValue(seriesId, double.Parse(values[0], CultureInfo.InvariantCulture)
                                        , double.Parse(values[1], CultureInfo.InvariantCulture));
                                }
                            }
                            else if (messageId == "Message")
                            {
                                experimentVM.addStatusInfoLine(messageContent);
                            }
                            else if (messageId == "End")
                            {
                                if (messageContent == "Ok")
                                {
                                    logMessage("Job finished sucessfully");
                                    experimentVM.state = MonitoredExperimentViewModel.ExperimentState.WAITING_RESULT;
                                }
                                else
                                {
                                    logMessage("Remote job execution wasn't successful");
                                    //Right now, my view on adding failed experiments back to the pending exp. list:
                                    //Some experiments may fail because the parameters are just invalid (i.e. FAST)
                                    //Much more likely than a network-related error or some other user-related problem
                                    //m_failedExperiments.Add(experimentVM);
                                    experimentVM.state = MonitoredExperimentViewModel.ExperimentState.ERROR;
                                }
                            }
                        }
                        else
                        {
                            if (experimentId == XMLStream.m_defaultMessageType)
                            {
                                //if (content == CJobDispatcher.m_endMessage)
                                {
                                    //job results can be expected to be sent back even if some of the tasks failed
                                    logMessage("Receiving job results");
                                    m_monitoredExperiments.ForEach((exp) => exp.state = MonitoredExperimentViewModel.ExperimentState.RECEIVING);
                                    m_herdAgent.status = "Receiving output files";
                                    bool bret = await m_shepherd.ReceiveJobResult(m_cancelToken);
                                    m_monitoredExperiments.ForEach((exp) => exp.state = MonitoredExperimentViewModel.ExperimentState.FINISHED);
                                    m_herdAgent.status = "Finished";
                                    logMessage("Job results received");
                                    return this;
                                }
                            }
                        }
                        xmlItem = m_shepherd.m_xmlStream.processNextXMLItem();
                    }
                }
            }

            catch (OperationCanceledException)
            {
                //quit remote jobs

                logMessage("Cancellation requested by user");
                m_shepherd.writeMessage(Shepherd.m_quitMessage, true);
                await m_shepherd.readAsync(new CancellationToken()); //we synchronously wait until we get the ack from the client

                m_monitoredExperiments.ForEach((exp) => { exp.resetState(); });
                m_herdAgent.status = "";
            }
            catch (Exception ex)
            {
                logMessage("Unhandled exception in Badger.sendJobAndMonitor(). Agent " + m_herdAgent.ipAddress);
                logMessage(ex.ToString());
                m_failedExperiments.Clear();
                foreach (MonitoredExperimentViewModel exp in m_monitoredExperiments) m_failedExperiments.Add(exp);
                Console.WriteLine(ex.StackTrace);
            }
            finally
            {
                logMessage("Disconnected from herd agent " + m_herdAgent.ipAddress);
                m_shepherd.disconnect();
            }
            return this;
        }
    }

    /// <summary>
    /// 
    /// </summary>
    public class ExperimentQueueMonitorViewModel : PropertyChangedBase
    {
        private List<HerdAgentViewModel> m_herdAgentList;
        private ObservableCollection<MonitoredExperimentViewModel> m_monitoredExperimentBatchList
            = new ObservableCollection<MonitoredExperimentViewModel>();
        private List<MonitoredExperimentViewModel> m_pendingExperiments = new List<MonitoredExperimentViewModel>();

        public ObservableCollection<MonitoredExperimentViewModel> monitoredExperimentBatchList
        { get { return m_monitoredExperimentBatchList; } }

        private CancellationTokenSource m_cancelTokenSource;

        //log stuff: a delegate log function must be passed via setLogFunction
        private Logger.LogFunction logFunction = null;
        private PlotViewModel m_evaluationMonitor;

        private double m_globalProgress;
        public double GlobalProgress
        {
            get { return m_globalProgress; }
            set
            {
                m_globalProgress = value;
                NotifyOfPropertyChange(() => GlobalProgress);
            }
        }

        private Stopwatch m_experimentTimer;
        public Stopwatch ExperimentTimer { get { return m_experimentTimer; } }

        private string m_estimatedEndTimeText = "";
        public string estimatedEndTime
        {
            get { return m_estimatedEndTimeText; }
            set
            {
                m_estimatedEndTimeText = value;
                NotifyOfPropertyChange(() => estimatedEndTime);
            }
        }

        // ---- TEST ---- //
        // TODO: Review this between TEST comment lines!!!
        private BindableCollection<LoggedExperimentViewModel> m_loggedExperiments
            = new BindableCollection<LoggedExperimentViewModel>();
        public BindableCollection<LoggedExperimentViewModel> loggedExperiments
        {
            get { return m_loggedExperiments; }
            set { m_loggedExperiments = value; NotifyOfPropertyChange(() => loggedExperiments); }
        }

        private void loadLoggedExperiment(XmlNode node)
        {
            LoggedExperimentViewModel newExperiment = new LoggedExperimentViewModel(node, null);
            loggedExperiments.Add(newExperiment);
        }

        // ---- TEST ---- //

        public ExperimentQueueMonitorViewModel(List<HerdAgentViewModel> freeHerdAgents,
            List<ExperimentalUnit> experiments, PlotViewModel evaluationMonitor,
            Logger.LogFunction logFunctionDelegate, string batchFileName)
        {
            m_bRunning = false;
            m_experimentTimer = new Stopwatch();
            m_evaluationMonitor = evaluationMonitor;
            m_herdAgentList = freeHerdAgents;
            logFunction = logFunctionDelegate;

            SimionFileData.LoadExperimentBatchFile(loadLoggedExperiment, batchFileName);

            foreach (var experiment in loggedExperiments)
            {
                List<string> prerequisites = new List<string>();

                foreach (var prerequisite in experiment.Prerequisites)
                    prerequisites.Add(prerequisite.Value);

                foreach (var unit in experiment.expUnits)
                {
                    MonitoredExperimentViewModel monitoredExperiment =
                    new MonitoredExperimentViewModel(unit, experiment.ExeFile, prerequisites, evaluationMonitor, this);
                    m_monitoredExperimentBatchList.Add(monitoredExperiment);
                    m_pendingExperiments.Add(monitoredExperiment);
                }
            }

            /*
            foreach (ExperimentalUnit exp in experiments)
            {
                MonitoredExperimentViewModel monitoredExperiment =
                    new MonitoredExperimentViewModel(exp, evaluationMonitor, this);
                m_monitoredExperimentBatchList.Add(monitoredExperiment);
                m_pendingExperiments.Add(monitoredExperiment);
            }
            */

            NotifyOfPropertyChange(() => monitoredExperimentBatchList);
        }

        /// <summary>
        ///     Express progress as a percentage unit to fill the global progress bar.
        /// </summary>
        public void updateGlobalProgress()
        {
            GlobalProgress = calculateGlobalProgress();

            if (GlobalProgress > 0.0 && GlobalProgress < 100.0)
                estimatedEndTime = "Estimated time to end: "
                    + System.TimeSpan.FromSeconds(m_experimentTimer.Elapsed.TotalSeconds
                    * ((100 - GlobalProgress) / GlobalProgress)).ToString(@"hh\:mm\:ss");
            else
                estimatedEndTime = "";
        }

        /// <summary>
        ///     Calculate the global progress of experiments in queue.
        /// </summary>
        /// <returns>The progress as a percentage.</returns>
        public double calculateGlobalProgress()
        {
            double sum = 0.0;
            foreach (MonitoredExperimentViewModel exp in m_monitoredExperimentBatchList)
                sum += exp.progress; //<- these are expressed as percentages

            return 100 * (sum / (m_monitoredExperimentBatchList.Count * 100));
        }


        private bool m_bRunning;

        public bool bRunning
        {
            get { return m_bRunning; }
            set { m_bRunning = value; NotifyOfPropertyChange(() => bRunning); }
        }

        private bool m_bFinished = false;
        public bool bFinished
        {
            get { return m_bFinished; }
            set { m_bFinished = value; NotifyOfPropertyChange(() => bFinished); }
        }

        public async void runExperimentsAsync(bool monitorProgress, bool receiveJobResults)
        {
            bRunning = true;
            m_cancelTokenSource = new CancellationTokenSource();

            List<Task<ExperimentBatch>> experimentBatchTaskList = new List<Task<ExperimentBatch>>();
            List<ExperimentBatch> experimentBatchList = new List<ExperimentBatch>();

            //assign experiments to free agents
            assignExperiments(ref m_pendingExperiments, ref m_herdAgentList, ref experimentBatchList,
                m_cancelTokenSource.Token, logFunction);
            try
            {
                while ((experimentBatchList.Count > 0 || experimentBatchTaskList.Count > 0
                    || m_pendingExperiments.Count > 0) && !m_cancelTokenSource.IsCancellationRequested)
                {
                    foreach (ExperimentBatch batch in experimentBatchList)
                    {
                        experimentBatchTaskList.Add(batch.sendJobAndMonitor());
                    }
                    //all pending experiments sent? then we await completion to retry in case something fails
                    if (m_pendingExperiments.Count == 0)
                    {
                        Task.WhenAll(experimentBatchTaskList).Wait();
                        logFunction("All the experiments have finished");
                        break;
                    }

                    //wait for the first agent to finish and give it something to do
                    Task<ExperimentBatch> finishedTask = await Task.WhenAny(experimentBatchTaskList);
                    ExperimentBatch finishedTaskResult = await finishedTask;
                    logFunction("Job finished: " + finishedTaskResult.ToString());
                    experimentBatchTaskList.Remove(finishedTask);

                    if (finishedTaskResult.failedExperiments.Count > 0)
                    {
                        foreach (MonitoredExperimentViewModel exp in finishedTaskResult.failedExperiments)
                            m_pendingExperiments.Add(exp);
                        logFunction(finishedTaskResult.failedExperiments.Count + " failed experiments enqueued again for further trials");
                    }

                    //just in case the freed agent hasn't still been discovered by the shepherd
                    if (!m_herdAgentList.Contains(finishedTaskResult.herdAgent))
                        m_herdAgentList.Add(finishedTaskResult.herdAgent);

                    //assign experiments to free agents
                    assignExperiments(ref m_pendingExperiments, ref m_herdAgentList, ref experimentBatchList,
                        m_cancelTokenSource.Token, logFunction);
                }
            }
            catch (Exception ex)
            {
                logFunction("Exception in runExperimentQueueRemotely()");
                logFunction(ex.StackTrace);
            }
            finally
            {
                if (m_pendingExperiments.Count == 0)
                    bFinished = true; // used to enable the "View reports" button

                bRunning = false;
                m_cancelTokenSource.Dispose();
            }
        }

        /// <summary>
        ///     Stops all experiments in progress.
        /// </summary>
        public void StopExperiments()
        {
            if (m_bRunning && m_cancelTokenSource != null)
                m_cancelTokenSource.Cancel();
        }

        private int batchId = 0;

        /// <summary>
        ///     Assigns experiments to availables herd agents.
        /// </summary>
        /// <param name="pendingExperiments"></param>
        /// <param name="freeHerdAgents"></param>
        /// <param name="experimentAssignments"></param>
        /// <param name="cancelToken"></param>
        /// <param name="logFunction"></param>
        public void assignExperiments(ref List<MonitoredExperimentViewModel> pendingExperiments,
            ref List<HerdAgentViewModel> freeHerdAgents, ref List<ExperimentBatch> experimentAssignments,
            CancellationToken cancelToken, Logger.LogFunction logFunction = null)
        {
            experimentAssignments.Clear();
            List<MonitoredExperimentViewModel> monitoredExperimentViewModels;

            while (pendingExperiments.Count > 0 && freeHerdAgents.Count > 0)
            {
                HerdAgentViewModel agentVM = freeHerdAgents[0];
                freeHerdAgents.RemoveAt(0);
                //usedHerdAgents.Add(agentVM);
                int numProcessors = Math.Max(1, agentVM.numProcessors - 1); //we free one processor

                monitoredExperimentViewModels = new List<MonitoredExperimentViewModel>();
                int numPendingExperiments = pendingExperiments.Count;

                for (int i = 0; i < Math.Min(numProcessors, numPendingExperiments); i++)
                {
                    monitoredExperimentViewModels.Add(pendingExperiments[0]);
                    pendingExperiments.RemoveAt(0);
                }

                experimentAssignments.Add(new ExperimentBatch("batch-" + batchId, monitoredExperimentViewModels,
                    agentVM, m_evaluationMonitor, cancelToken, logFunction));
                ++batchId;
            }
        }
    }
}
