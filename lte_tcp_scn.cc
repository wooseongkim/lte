#include "ns3/lte-helper.h"
#include "ns3/epc-helper.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/lte-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/config-store.h"

using namespace ns3;
template <typename T>
void NewValueTracerIntoStream (std::ofstream *ofs, T oldValue, T newValue) {
	*ofs << Simulator::Now ().GetSeconds () << '\t' << newValue <<std::endl;
}
void NewTimeValueTracerIntoStream (std::ofstream *ofs, Time oldValue, Time newValue) {
	*ofs << Simulator::Now ().GetSeconds () << '\t' << newValue.GetSeconds () << std::endl;
}
void DoBulkSendApplicationTcpTrace (Ptr<BulkSendApplication> application, std::string traceFilePrefix, Time traceEnd) {
	using namespace std;
	Ptr<TcpSocketBase> socket = application->GetSocket ()->GetObject<TcpSocketBase> ();

	ofstream *cwndFile = new ofstream ((traceFilePrefix + ".cwnd").c_str());
	socket->TraceConnectWithoutContext ("CongestionWindow", MakeBoundCallback (&NewValueTracerIntoStream<uint32_t>, cwndFile));
	Simulator::Schedule (traceEnd, &ofstream::close, cwndFile);

	ofstream *rttFile = new ofstream ((traceFilePrefix + ".rtt").c_str());
	socket->TraceConnectWithoutContext ("RTT", MakeBoundCallback (&NewTimeValueTracerIntoStream, rttFile));
	Simulator::Schedule (traceEnd, &ofstream::close, rttFile);

	ofstream *rtoFile = new ofstream ((traceFilePrefix + ".rto").c_str ());
	socket->TraceConnectWithoutContext ("RTO", MakeBoundCallback (&NewTimeValueTracerIntoStream, rtoFile));
	Simulator::Schedule (traceEnd, &ofstream::close, rtoFile);

	ofstream *timeoutFile = new ofstream ((traceFilePrefix + ".timeout").c_str ());
	socket->TraceConnectWithoutContext ("NUM_TIMEOUTS", MakeBoundCallback (&NewValueTracerIntoStream<uint32_t>, timeoutFile));
	Simulator::Schedule (traceEnd, &ofstream::close, timeoutFile);

	ofstream *frcFile = new ofstream ((traceFilePrefix + ".frc").c_str ());
	socket->TraceConnectWithoutContext ("NUM_FAST_RECOVERIES", MakeBoundCallback (&NewValueTracerIntoStream<uint32_t>, frcFile));
	Simulator::Schedule (traceEnd, &ofstream::close, frcFile);
}

void BulkSendApplicationTcpTrace (Ptr<BulkSendApplication> application, std::string traceFilePrefix, Time traceStart, Time traceEnd) {
	//Scheduling resolution: 1 ns
	Simulator::Schedule (traceStart + NanoSeconds (1), &DoBulkSendApplicationTcpTrace,
			application, traceFilePrefix, traceEnd);
}
void NewSeqNumTracer (std::ofstream *seqFile, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface) {
	Ptr<Packet> pkt = packet->Copy ();

	Ipv4Header ipHeader;
	pkt->RemoveHeader (ipHeader);

	TcpHeader tcpHeader;
	pkt->PeekHeader (tcpHeader);
	*seqFile << Simulator::Now ().GetSeconds () << "\t" << tcpHeader.GetSequenceNumber () << std::endl;
}

void ReceiverTcpTrace (Ptr<Ipv4L3Protocol> ipv4, std::string traceFilePrefix, Time traceEnd) {
	using namespace std;
	ofstream *rseqFile = new ofstream ((traceFilePrefix + ".rseq").c_str ());
	ipv4->TraceConnectWithoutContext ("Rx", MakeBoundCallback (&NewSeqNumTracer, rseqFile));
	Simulator::Schedule (traceEnd, &ofstream::close, rseqFile);
}

int main (int argc, char *argv[])
{	
	uint16_t numberOfUEs = 4;
    //double simTime = 1.1;
	double distance = 60.0;
    //double interPacketInterval = 100;
	
	int packetSize = 1400;
	double applicationStart = 1.0;
	double applicationEnd = 101.0;
	double simulationEnd = applicationEnd + 1.0; 
	double ltesourcestart = applicationStart;
	std::string traceFilePrefix = "trace", traceFilePrefixLte;

	//Enough buffer
	Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (640000));
	Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (640000));
	
    CommandLine cmd;
#if 0

	cmd.AddValue("numberOfNodes", "Number of eNodeBs + UE pairs", numberOfNodes);
	cmd.AddValue("simTime", "Total duration of the simulation [s])", simTime);
	cmd.AddValue("distance", "Distance between eNBs [m]", distance);
	cmd.AddValue("interPacketInterval", "Inter packet interval [ms])", interPacketInterval);
	cmd.Parse(argc, argv);
#endif
	std::string traceFilePrefix = "trace";
	
	Ptr<LteHelper> lteHelper = CreateObject<LteHelper> ();
	Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper> ();
	lteHelper->SetEpcHelper (epcHelper);
	
	//LOAD default config; wkim-unclear to call this func.
	ConfigStore inputConfig;
	inputConfig.ConfigureDefaults();
	// parse again so you can override default values from the command line
	cmd.Parse(argc, argv);
  
	Ptr<Node> pgw = epcHelper->GetPgwNode ();

	NodeContainer remotesHostContainer, enbContainer, ueContainer;
	remotesHostContainer.Create (numberOfUEs);
	enbContainer.Create (1);
	ueContainer.Create (numberOfUEs);
	
	// Create the Internet
	InternetStackHelper internet;
    internet.Install (remotesHostContainer);

	
	//P2P link
	PointToPointHelper p2pHelper;
	p2pHelper.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("1Gb/s")));	//("DataRate", StringValue ("1Gbps"))
	p2pHelper.SetDeviceAttribute ("Mtu", UintegerValue (1500));
	p2pHelper.SetChannelAttribute ("Delay", TimeValue (Seconds (0.010)));		//("Delay", StringValue ("5ms"))
	
	//Netdeivce container, 	Temporal containers
	NetDeviceContainer p2pDevs;

	//Address
	Ipv4AddressHelper ipv4;
	char network[256];
	
	//RemotesForUes - PGW w/ IP address allocation
	Ipv4InterfaceContainer hostIpIfacesContainer;
	for (uint32_t i = 0; i < remotesHostContainer.GetN (); i++){
		p2pDevs = p2pHelper.Install (remotesHostContainer.Get (i), pgw);
		sprintf(network, "1.1.%d.0", i + 1);
		ipv4.SetBase (network, "255.255.255.0");
		hostIpIfacesContainer = ipv4.Assign (p2pDevs);	//??
	}
	// interface 0 is localhost, 1 is the p2p device
    //Ipv4Address remoteHostAddr = hostIpIfacesContainer.GetAddress (1);
	
	//Mobility
	Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
    for (uint16_t i = 0; i < numberOfUEs+1; i++)
	{
	positionAlloc->Add (Vector(distance * i, 0, 0));
	}
	MobilityHelper mobility; // (0, 0), ConstantPosition
	//mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
	//mobility.SetPositionAllocator(positionAlloc);
	mobility.Install(enbContainer);
	mobility.Install(ueContainer);

	// Install LTE Devices to the nodes
	NetDeviceContainer enbLteDevs = lteHelper->InstallEnbDevice (enbContainer);
	NetDeviceContainer ueLteDevs = lteHelper->InstallUeDevice (ueContainer);
  
	// Install the IP stack on the UEs
	internet.Install (ueContainer);
	Ipv4InterfaceContainer ueIpIfaceContainer;
	ueIpIfaceContainer = epcHelper->AssignUeIpv4Address (NetDeviceContainer (ueLteDevs)); //epc->AssignUeIpv4Address (ueDevs); //"7.0.0.0", "255.0.0.0"

	//Routing
	Ipv4StaticRoutingHelper routing;

	//RemotesForUes -> UEs
    for (uint32_t i = 0; i < remotesHostContainer.GetN (); i++){
        Ptr<Ipv4StaticRouting> remoteHostStaticRoutingEntity = routing.GetStaticRouting (remotesHostContainer.Get (i)->GetObject<Ipv4> ());
		remoteHostStaticRoutingEntity->AddNetworkRouteTo (Ipv4Address ("7.0.0.0"), Ipv4Mask ("255.0.0.0"), 1);
	}
	//UEs -> RemotesForUes
	for (uint32_t i = 0; i < ueContainer.GetN (); i++){
		Ptr<Ipv4StaticRouting> ueStaticRoutingEntity = routing.GetStaticRouting (ueContainer.Get (i)->GetObject<Ipv4> ());
		ueStaticRoutingEntity->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);
	}
	
	
	//lte->Attach (ueDevs, enbDev.Get (0));	
	// Attach one UE per eNodeB
    for (uint16_t i = 0; i < numberOfUEs; i++)
	{
		lteHelper->Attach (ueLteDevs.Get(i), enbLteDevs.Get(0));
		// side effect: the default EPS bearer will be activated
	}
  
	//Application
	uint16_t dlPort = 1234;
    //uint16_t ulPort = 2000;
    //uint16_t otherPort = 3000;
	ApplicationContainer clientApps;		//ueSinkApp
	ApplicationContainer serverApps;
	
		
	for (uint32_t i = 0; i < remotesHostContainer.GetN (); i++){
		BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (ueIpIfaceContainer.GetAddress (i), dlPort));
		source.SetAttribute ("MaxBytes", UintegerValue (0)); //Infinite backlog?
		source.SetAttribute ("SendSize", UintegerValue (packetSize)); //Default: 512B
		serverApps.Add (source.Install (remotesHostContainer.Get (i)));

		ltesourcestart = ltesourcestart + 0.2;
		ApplicationContainer tmpApp;
		tmpApp.Add (serverApps.Get(serverApps.GetN () - 1));
		tmpApp.Start (Seconds (ltesourcestart));	
		
		if(i==0 && i !=remotesHostContainer.GetN()-1) {
			/*forlte = i;*/
			traceFilePrefixLte = traceFilePrefix + "-ues"/* + char('0'+forlte)*/;
			BulkSendApplicationTcpTrace (sourceApp.Get(sourceApp.GetN ()-1)->GetObject<BulkSendApplication> (), traceFilePrefixLte, Seconds (ltesourcestart + 0.2), Seconds (simulationEnd));
			ReceiverTcpTrace (ues.Get(i)->GetObject<Ipv4L3Protocol> (), traceFilePrefixLte, Seconds (simulationEnd));
		}

		PacketSinkHelper sink ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), dlPort));
		clientApps.Add (sink.Install (ueContainer.Get (i)));
	}
  
	serverApps.Stop (Seconds (applicationEnd));
	clientApps.Start (Seconds (ltesourcestart));
	clientApps.Stop (Seconds (applicationEnd));
	
	Simulator::Stop (Seconds (simulationEnd)); 
	Simulator::Run ();
	Simulator::Destroy (); return 0;
	
}

	
