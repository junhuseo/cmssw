import FWCore.ParameterSet.Config as cms

filteredLayerClustersCLUE3DHighL1Seeded = cms.EDProducer("FilteredLayerClustersProducer",
    LayerClusters = cms.InputTag("hgcalLayerClustersL1Seeded"),
    LayerClustersInputMask = cms.InputTag("hgcalLayerClustersL1Seeded","InitialLayerClustersMask"),
    algo_number = cms.int32(8),
    clusterFilter = cms.string('ClusterFilterByAlgoAndSize'),
    iteration_label = cms.string('CLUE3DHigh'),
    max_cluster_size = cms.int32(9999),
    max_layerId = cms.int32(9999),
    mightGet = cms.optional.untracked.vstring,
    min_cluster_size = cms.int32(2),
    min_layerId = cms.int32(0)
)
