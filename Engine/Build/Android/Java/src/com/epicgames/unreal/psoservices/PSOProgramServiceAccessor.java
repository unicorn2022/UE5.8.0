// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.unreal.psoservices;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.os.Message;
import android.os.Messenger;
import android.os.RemoteException;
import android.os.SharedMemory;
import android.os.StrictMode;
import android.os.Trace;
import android.system.ErrnoException;
import android.util.Log;
import android.os.ParcelFileDescriptor;
import android.os.SystemClock;

import com.epicgames.unreal.GameActivity;
import com.epicgames.unreal.Logger;
import com.google.android.gms.games.Game;

import java.io.BufferedReader;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Semaphore;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.locks.ReadWriteLock;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import java.util.concurrent.TimeUnit;
import java.nio.file.Files;

public class PSOProgramServiceAccessor
{
	static final boolean bEnableTrace = false;
	static void beginTrace(String msg)
	{
		if( bEnableTrace )
		{
			Trace.beginSection(msg);
		}
	}
	static void endTrace()
	{
		if( bEnableTrace )
		{
			Trace.endSection();
		}
	}

	static final Class<?>[] ServiceClassTypes = new Class<?>[] {
		OGLProgramService.class
		,OGLProgramService1.class
		,OGLProgramService2.class
		,OGLProgramService3.class
		,OGLProgramService4.class
		,OGLProgramService5.class
		,OGLProgramService6.class
		,OGLProgramService7.class
	};

	static final Class<?>[] VulkanServiceClassTypes = new Class<?>[] {
		VulkanProgramService.class
		,VulkanProgramService1.class
		,VulkanProgramService2.class
		,VulkanProgramService3.class
		,VulkanProgramService4.class
		,VulkanProgramService5.class
		,VulkanProgramService6.class
		,VulkanProgramService7.class
	};

	private static final String TAG = "PSOProgramServiceAccessor";
	private static PSOProgramServiceAccessor _PSOProgramServiceAccessor = null;

	OGLServiceInstance[] ServiceInstances;
	private GameActivity mActivity;
	private Context mContext;

	static class JobResponse
	{
		public android.os.Bundle data;
		public enum ResponseStateEnum { Uninitialized, Pending, Responded}
		public ResponseStateEnum ResponseState = ResponseStateEnum.Uninitialized;
		public final Object SyncObj = new Object();
	}

	private static final ConcurrentHashMap<Integer, JobResponse> SyncObs = new ConcurrentHashMap<>();

	/**
	 * Handler for incoming messages from the PSOProgramService(s).
	 */
	static class IncomingHandler extends Handler
	{
		public IncomingHandler(Looper inLooper)
		{
			super(inLooper);
		}

		@Override
		public void handleMessage(Message msg)
		{
			switch (msg.what)
			{
				case PSOProgramService.MSG_LINKPROGRAM_RESPONSE:
				{
					beginTrace("PSOProgramService.MSG_LINKPROGRAM_RESPONSE");
					byte[] CompiledResult = msg.getData().getByteArray(PSOProgramService.CompiledProgram_Key);
					byte[] JobContext = msg.getData().getByteArray(PSOProgramService.JobContext_Key);
					int JobID = msg.getData().getInt(PSOProgramService.JobID_Key, -1);
					int ServiceID = msg.getData().getInt(PSOProgramService.ServiceID_Key, -1);
					// not needed here, since we'll just respond with the same data (Bundle)
					//float CompilationDuration = msg.getData().getFloat(PSOProgramService.CompilationDuration_Key, -1.0f);

					try
					{
						ProgramServiceAccessorlock.readLock().lock();
						// ServiceInstances is nulled by StopServices() during shutdown; a response
						// message that was already queued can reach here after the write lock is released.
						OGLServiceInstance[] Instances = _PSOProgramServiceAccessor.ServiceInstances;
						if (Instances != null && ServiceID >= 0 && ServiceID < Instances.length)
						{
							Instances[ServiceID].PendingJobs.decrementAndGet();
						}
					}
					finally
					{
						ProgramServiceAccessorlock.readLock().unlock();
					}

					{
						beginTrace("PSOProgramService.MSG_LINKPROGRAM_RESPONSE " + JobID);
						//Log.verbose("CompletedLinkBroadcastReceiver for job " + JobID + " program length " + (CompiledResult == null ? 0 : CompiledResult.length) + " context size " + (JobContext == null? 0 : JobContext.length));
						JobResponse response = SyncObs.get(JobID);
						if (response != null)
						{
							synchronized (response.SyncObj)
							{
								response.data = msg.getData();
								response.ResponseState = JobResponse.ResponseStateEnum.Responded;
								response.SyncObj.notifyAll();
							}
						} else
						{
							Log.error("Job " + JobID + " completed but request already timed out.");
						}
						endTrace();
					}
					endTrace();
					break;
				}
				default:
					super.handleMessage(msg);
			}
		}
	}

	static HandlerThread PSOProgramAccessorHandlerThread;

	static AtomicBoolean bIsReady = new AtomicBoolean(false);
	static Semaphore MaxLiveRequestsSemaphore;
	static int MaxLiveRequestPermitCount = 0;
	static Messenger mReplyToMe;

	final AtomicInteger LastServiceIdx = new AtomicInteger(0);

	private static final ReadWriteLock ProgramServiceAccessorlock = new ReentrantReadWriteLock();

	private static boolean IsNullOrEmpty(String string) { return string == null || string.isEmpty(); }

	private boolean bServicePriHigh = false;
	public void SetServicePriority(boolean bHigh) { bServicePriHigh = bHigh; }

	// When  >0 the PSO init data sent to each service is retained. a total of RetainedProgramInitDataHistoryLimit * servicecount files will be stored. 
	private static int RetainedProgramInitDataHistoryLimit = 0;

	private static String RetainedProgramInitDataHistoryPath = "/PSORetained/";
	public PSOProgramServiceAccessor()
	{
		//StrictMode.enableDefaults();
		//StrictMode.setVmPolicy(new StrictMode.VmPolicy.Builder()
		//	.detectLeakedClosableObjects()
		//	.penaltyLog()
		//	.build());
	}

	private static final Logger Log = GameActivity.Log;
	public void Init(GameActivity InActivity)
	{
		mActivity = InActivity;
		mContext = mActivity.getApplicationContext();
		ClearServiceLogDirs();
		_PSOProgramServiceAccessor = this;
	}

	// deletes a directory and its content. 
	// Note: Doesn't delete subdirs.
	private static void DeleteDir(String DirToRemove)
	{
		try
		{
			File deleteDir = new File(DirToRemove);
			boolean bDeleteDirExists = deleteDir.exists();
			if (bDeleteDirExists)
			{
				File[] deletedirContent = deleteDir.listFiles();
				if (deletedirContent != null)
				{
					for (File deleteFile : deletedirContent)
					{
						deleteFile.delete();
					}
				}
				deleteDir.delete();
			}
		}
		catch(Exception e)
		{
			Log.error( "DeleteDir("+DirToRemove+") - FAIL " + e);
			e.printStackTrace();
		}
	}
	private void ClearServiceLogDirs()
	{
		String OGLLogDir = mActivity.getFilesDir()+PSOProgramService.LogDir;
		DeleteDir(OGLLogDir);
	}

	public static boolean AndroidThunkJava_StartRemoteProgramLink(int numServices, boolean bUseRobustEGLContext, boolean bUseVulkan)
	{
		boolean bSuccess = false;
		if( _PSOProgramServiceAccessor != null)
		{
			try
			{
				// always clear the retained program dir
				String RetainedProgramDestDir = _PSOProgramServiceAccessor.GetRetainedProgramInitDataPath();
				DeleteDir(RetainedProgramDestDir);
				if(RetainedProgramInitDataHistoryLimit > 0)
				{
					Files.createDirectories( Paths.get( RetainedProgramDestDir ) );
				}
				ProgramServiceAccessorlock.writeLock().lock();
				PSOProgramAccessorHandlerThread = new HandlerThread("PSOProgramAccessorThread");
				PSOProgramAccessorHandlerThread.start();
				mReplyToMe = new Messenger(new IncomingHandler(PSOProgramAccessorHandlerThread.getLooper()));

				// rate limit the total number of inflight PSO compile requests. shut down waits for all jobs to complete and this reduces the potential wait time.
				// calling threads wait for a request permit to become available or shutdown is issued.
				MaxLiveRequestPermitCount = numServices * 3;
				MaxLiveRequestsSemaphore = new Semaphore(MaxLiveRequestPermitCount);
				if(bUseVulkan)
				{
					bSuccess = _PSOProgramServiceAccessor.StartVulkanServices(numServices);
				}
				else
				{
					bSuccess = _PSOProgramServiceAccessor.StartServices(numServices, bUseRobustEGLContext);
				}
			}
			catch (Exception e)
			{
				e.printStackTrace();
			}
			finally
			{
				ProgramServiceAccessorlock.writeLock().unlock();
			}
		}

		if(!bSuccess)
		{
			AndroidThunkJava_StopRemoteProgramLink();
		}
		bIsReady.set(bSuccess);
		return bSuccess;
	}

	public static void AndroidThunkJava_StopRemoteProgramLink()
	{
		if( _PSOProgramServiceAccessor != null)
		{
			bIsReady.set(false);
			// acquire all of the requests, this causes us to wait for all in progress compiles.
			MaxLiveRequestsSemaphore.acquireUninterruptibly(MaxLiveRequestPermitCount);
			try
			{
				ProgramServiceAccessorlock.writeLock().lock();
				_PSOProgramServiceAccessor.StopServices();
				PSOProgramAccessorHandlerThread.quitSafely();
				mReplyToMe = null;
			}
			catch (Exception e)
			{
				e.printStackTrace();
			}
			finally
			{
				ProgramServiceAccessorlock.writeLock().unlock();
			}
		}
	}

	public static boolean AndroidThunkJava_AreProgramServicesReady()
	{
		boolean bSuccess = _PSOProgramServiceAccessor.ServiceInstances.length > 0;
		for (OGLServiceInstance ServiceInstance : _PSOProgramServiceAccessor.ServiceInstances )
		{
			bSuccess = bSuccess && ServiceInstance.IsServiceBound();
		}
		return bSuccess;
	}

	public static boolean AndroidThunkJava_HaveServicesFailed()
	{
		boolean bHasFailed = _PSOProgramServiceAccessor.ServiceInstances.length == 0;
		for (OGLServiceInstance ServiceInstance : _PSOProgramServiceAccessor.ServiceInstances )
		{
			bHasFailed = bHasFailed || ServiceInstance.HasBindFailed();
		}
		return bHasFailed;
	}

	boolean StartServices(int numServices, boolean bUseRobustEGLContext)
	{
		numServices = Math.max(1, Math.min(numServices, ServiceClassTypes.length));

		ServiceInstances = new OGLServiceInstance[numServices];
		for(int i = 0; i< numServices ;i++)
		{
			ServiceInstances[i] = new OGLServiceInstance(ServiceClassTypes[i], bUseRobustEGLContext);
		}

		boolean bSuccess = true;
		for (OGLServiceInstance ServiceInstance : ServiceInstances )
		{
			bSuccess = bSuccess && ServiceInstance.doBind();
		}
		return bSuccess;
	}

	boolean StartVulkanServices(int numServices)
	{
		numServices = Math.max(1, Math.min(numServices, VulkanServiceClassTypes.length));

		ServiceInstances = new OGLServiceInstance[numServices];
		for(int i = 0; i< numServices ;i++)
		{
			ServiceInstances[i] = new OGLServiceInstance(VulkanServiceClassTypes[i], false);
		}

		boolean bSuccess = true;
		for (OGLServiceInstance ServiceInstance : ServiceInstances )
		{
			bSuccess = bSuccess && ServiceInstance.doBind();
		}
		return bSuccess;
	}

	public String GetRetainedProgramInitDataPath()
	{
		return mActivity.getFilesDir() + RetainedProgramInitDataHistoryPath;
	}
	
	public static void AndroidThunkJava_SetProgramInitDataRetainLimit(int InRetainedProgramInitDataHistoryLimit)
	{
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1)
		{
			RetainedProgramInitDataHistoryLimit = InRetainedProgramInitDataHistoryLimit;
			try
			{
				ProgramServiceAccessorlock.readLock().lock();
				if( _PSOProgramServiceAccessor != null)
				{
					String DestDir = _PSOProgramServiceAccessor.GetRetainedProgramInitDataPath();
					Files.createDirectories( Paths.get( DestDir ) );
				}
			}
			catch (Exception e)
			{
				Log.error( "SetProgramInitDataRetainLimit FAIL " + e);
			}
		    finally
		    {
			    ProgramServiceAccessorlock.readLock().unlock();
		    }
		}
		else
		{
			Log.error( "SetProgramInitDataRetainLimit requires API 27 and above.");
		}
	}

	public static String AndroidThunkJava_GetRetainedProgramInitDataPath()
	{
		try
		{
			ProgramServiceAccessorlock.readLock().lock();
			if( _PSOProgramServiceAccessor != null)
			{
				return _PSOProgramServiceAccessor.GetRetainedProgramInitDataPath();
			}
		}
		finally
		{
			ProgramServiceAccessorlock.readLock().unlock();
		}
		return "";
	}

	void StopServices()
	{
		if(ServiceInstances != null)
		{
			Log.debug("Stopping OGL Program compile services");
			for (OGLServiceInstance ServiceInstance : ServiceInstances )
			{
				ServiceInstance.ReadBackServiceLog();
				ServiceInstance.doUnbindService();
			}
			ServiceInstances = null;
		}
	}

	private static final AtomicInteger JobID = new AtomicInteger(5);

	public static class JNIProgramLinkResponse
	{
		// Changes here must be reflected in AndroidOpenGL.cpp which reads this struct via JNI queries.
		boolean bCompileSuccess;
		String ErrorMessage;
		byte[] CompiledProgram;
		int SHMOutputHandle;
		float CompilationDuration;
	}

	static final private Object ProgramLinkLock = new Object();
	public static JNIProgramLinkResponse AndroidThunkJava_OGLRemoteProgramLink(byte[] ContextData, long PriorityInfo, String VertexShader, String PixelShader, String ComputeShader, boolean bAllowTimeOuts)
	{
		while( bIsReady.get())
		{
			try
			{
				if( MaxLiveRequestsSemaphore.tryAcquire(1, 100, TimeUnit.MILLISECONDS) )
				{
					JNIProgramLinkResponse returnResponse = null;
					if(GameActivity.IsActivityPaused())
					{
						// one at a time when backgrounded.
						synchronized (ProgramLinkLock)
						{
							returnResponse = OGLRemoteProgramLink_internal(ContextData, PriorityInfo, VertexShader, PixelShader, ComputeShader, bAllowTimeOuts);
						}
					}
					else
					{
						returnResponse = OGLRemoteProgramLink_internal(ContextData, PriorityInfo, VertexShader, PixelShader, ComputeShader, bAllowTimeOuts);
					}
					MaxLiveRequestsSemaphore.release(1);
					return returnResponse;
				}
			}
			catch (InterruptedException e)
			{
				Log.error( "interrupted " + e);
			}
		}
		Log.error("Failed to compile PSO services not ready.");
		return null;
	}

	private static JNIProgramLinkResponse OGLRemoteProgramLink_internal(byte[] ContextData, long PriorityInfo, String VertexShader, String PixelShader, String ComputeShader, boolean bAllowTimeOuts)
	{
		try
		{
			ProgramServiceAccessorlock.readLock().lock();
			if (_PSOProgramServiceAccessor == null)
			{
				Log.error("AndroidThunkJava_OGLRemoteProgramLink Called too early ");
				return null;
			}
			if (_PSOProgramServiceAccessor.ServiceInstances.length == 0)
			{
				Log.error( "AndroidThunkJava_OGLRemoteProgramLink not enabled.");
				return null;
			}

			int ThisJobID = JobID.incrementAndGet();
			beginTrace("AndroidThunkJava_OGLRemoteProgramLink " + ThisJobID);
			Message msg = Message.obtain(null, PSOProgramService.MSG_LINKPROGRAM, 0, 0);
			Bundle params = new Bundle();
			if (IsNullOrEmpty(ComputeShader))
			{
				//Log.verbose("GFX compile job");
				if (IsNullOrEmpty(VertexShader))
				{
					Log.error("Failed to send compile job VS is null ");
					return null;
				}
				if (IsNullOrEmpty(PixelShader))
				{
					Log.error("Failed to send compile job PS is null ");
					return null;
				}
				params.putString(PSOProgramService.VS_Key, VertexShader);
				params.putString(PSOProgramService.PS_Key, PixelShader);
			}
			else
			{
				//Log.verbose("Compute compile job");
				params.putString(PSOProgramService.CS_Key, ComputeShader);
			}

			params.putByteArray(PSOProgramService.JobContext_Key, ContextData);

			params.putInt(PSOProgramService.JobID_Key, ThisJobID);
			params.putLong(PSOProgramService.Priority_Key, PriorityInfo);

			msg.replyTo = mReplyToMe;

			JobResponse pendingResponse = new JobResponse();
			synchronized (pendingResponse.SyncObj)
			{
				SyncObs.put(ThisJobID, pendingResponse);

				msg.setData(params);
				boolean bValidService = false;
				OGLServiceInstance thisInstance = null;
				// loop through the services to find a valid (bound) service, skip and attempt to rebind any unbound services.
				for(int i = 0; i<_PSOProgramServiceAccessor.ServiceInstances.length; i++)
				{
					int ServiceIdx = _PSOProgramServiceAccessor.LastServiceIdx.incrementAndGet() % _PSOProgramServiceAccessor.ServiceInstances.length;
					OGLServiceInstance testInstance = _PSOProgramServiceAccessor.ServiceInstances[ServiceIdx];
					if(testInstance.IsServiceBound())
					{
						// found a valid service.
						thisInstance = testInstance;
						params.putInt(PSOProgramService.ServiceID_Key, ServiceIdx);
						break;
					}
					else
					{
						// found an unbound service, attempt to rebind and continue;
						Log.warn("OGLRemoteProgramLink "+testInstance.Name()+" (" + ThisJobID + ") was unbound, rebinding and trying next service");
						testInstance.doBindService();
						testInstance.ReadBackServiceLog();
					}
				}

				//Log.verbose("OGLRemoteProgramLink dispatching to "+thisInstance.Name()+" (" + ThisJobID + ")");
				pendingResponse.ResponseState = JobResponse.ResponseStateEnum.Pending;
				if( thisInstance != null && thisInstance.SendMessage(msg) )
				{
					int pendingJobs = thisInstance.PendingJobs.incrementAndGet();

					long tStartTimeMS = System.currentTimeMillis();
					// if we're allowed to time out, wait for 10s before giving up on the compile job.
					long tTimeoutInMS = bAllowTimeOuts ? 10000 : Long.MAX_VALUE;

					while (pendingResponse.ResponseState != JobResponse.ResponseStateEnum.Responded)
					{
						// we must loop as waits can randomly wake.
						pendingResponse.SyncObj.wait(1000);

						if ((System.currentTimeMillis() - tStartTimeMS) >= tTimeoutInMS)
						{
							Log.error("OGLRemoteProgramLink TIMED OUT WAITING " + ThisJobID + " for " + (System.currentTimeMillis() - tStartTimeMS) + "ms. pending tasks "+thisInstance.PendingJobs.get()+"limit "+tTimeoutInMS);
							//timeout
							SyncObs.remove(ThisJobID);
							thisInstance.ReadBackServiceLog();
							return null;
						}
					}
					long totalWaitTimeMS = System.currentTimeMillis() - tStartTimeMS;
					if(totalWaitTimeMS > 2500)
					{
						Log.verbose("OGLRemoteProgramLink responded " + ThisJobID + " total wait time "+totalWaitTimeMS+" ms. pending tasks "+thisInstance.PendingJobs.get());
					}
				}
				else if( thisInstance != null)
				{
					// process communications failed somehow.
					SyncObs.remove(ThisJobID);
					thisInstance.ReadBackServiceLog();
					return null;
				}
				else
				{
					// failed to find a single bound service?
					Log.error("OGLRemoteProgramLink " + ThisJobID + " no valid bound services.");
					SyncObs.remove(ThisJobID);
					return null;
				}

			}

			SyncObs.remove(ThisJobID);

			byte[] EngineJobContext = pendingResponse.data.getByteArray(PSOProgramService.JobContext_Key);
			byte[] CompiledBinary = pendingResponse.data.getByteArray(PSOProgramService.CompiledProgram_Key);
			int JobID = pendingResponse.data.getInt(PSOProgramService.JobID_Key);
			float CompilationDuration = pendingResponse.data.getFloat(PSOProgramService.CompilationDuration_Key);

			//Log.verbose("OGLRemoteProgramLink handoff ("+JobID+"), ("+(EngineJobContext == null ? 0 : EngineJobContext.length)+", "+(CompiledBinary==null?0:CompiledBinary.length)+")");

			JNIProgramLinkResponse jniResponse = new JNIProgramLinkResponse();
			String fail = pendingResponse.data.getString(PSOProgramService.JobFail);
			jniResponse.bCompileSuccess = fail == null || fail.isEmpty();
			jniResponse.ErrorMessage = fail;
			jniResponse.CompiledProgram = CompiledBinary;
			jniResponse.CompilationDuration = CompilationDuration;

			return jniResponse;
		}
		catch (Exception e)
		{
			Log.error( "OGLRemoteProgramLink FAIL " + e);
		}
		finally
		{
			ProgramServiceAccessorlock.readLock().unlock();
			endTrace();
		}

		return null;
	}

	public static JNIProgramLinkResponse AndroidThunkJava_VKPSOGFXCompile(byte[] ContextData, long PriorityInfo, byte[] VertexShader, byte[] PixelShader, byte[] PSOData, byte[] PSOCacheData, boolean bAllowTimeOuts)
	{
		while( bIsReady.get() )
		{
			try
			{
				if( MaxLiveRequestsSemaphore.tryAcquire(1, 100, TimeUnit.MILLISECONDS) )
				{
					JNIProgramLinkResponse returnResponse = null;

					if(GameActivity.IsActivityPaused())
					{
						// one at a time when backgrounded.
						synchronized (ProgramLinkLock)
						{
							returnResponse = VKPSOGFXCompile_internal(ContextData, PriorityInfo, VertexShader, PixelShader, PSOData, PSOCacheData, bAllowTimeOuts);
						}
					}
					else
					{
						returnResponse = VKPSOGFXCompile_internal(ContextData, PriorityInfo, VertexShader, PixelShader, PSOData, PSOCacheData, bAllowTimeOuts);
					}
					MaxLiveRequestsSemaphore.release(1);
					return returnResponse;
				}
			}
			catch (InterruptedException e)
			{
				Log.error( "interrupted " + e);
			}
		}
		Log.error("Failed to compile PSO services not ready.");
		return null;
	}

	public static JNIProgramLinkResponse AndroidThunkJava_VKPSOGFXCompileShm(byte[] ContextData, long PriorityInfo, int SharedMemFD, long VertexShaderSize, long PixelShaderSize, long PSODataSize, long PSOCacheDataSize, boolean bAllowTimeOuts)
	{
		while( bIsReady.get() )
		{
			try
			{
				if( MaxLiveRequestsSemaphore.tryAcquire(1, 100, TimeUnit.MILLISECONDS) )
				{
					JNIProgramLinkResponse returnResponse = null;
					if(GameActivity.IsActivityPaused())
					{
						// one at a time when backgrounded.
						synchronized (ProgramLinkLock)
						{
							returnResponse = VKPSOGFXCompileShm_internal(ContextData, PriorityInfo, SharedMemFD, VertexShaderSize, PixelShaderSize, PSODataSize, PSOCacheDataSize, bAllowTimeOuts);
						}
					}
					else
					{
						returnResponse = VKPSOGFXCompileShm_internal(ContextData, PriorityInfo, SharedMemFD, VertexShaderSize, PixelShaderSize, PSODataSize, PSOCacheDataSize, bAllowTimeOuts);
					}
					MaxLiveRequestsSemaphore.release(1);
					return returnResponse;
				}
			}
			catch (InterruptedException e)
			{
				Log.error( "interrupted " + e);
			}
		}
		Log.error("Failed to compile PSO services not ready.");
		return null;
	}

	private static JNIProgramLinkResponse VKPSOGFXCompile_internal(byte[] ContextData, long PriorityInfo, byte[] VertexShader, byte[] PixelShader, byte[] PSOData, byte[] PSOCacheData, boolean bAllowTimeOuts)
	{
		try
		{
			ProgramServiceAccessorlock.readLock().lock();
			if (_PSOProgramServiceAccessor == null)
			{
				Log.error("AndroidThunkJava_VKPSOGFXCompile Called too early ");
				return null;
			}
			if (_PSOProgramServiceAccessor.ServiceInstances.length == 0)
			{
				Log.error( "AndroidThunkJava_VKPSOGFXCompile not enabled.");
				return null;
			}

			int ThisJobID = JobID.incrementAndGet();
			beginTrace("AndroidThunkJava_OGLRemoteProgramLink " + ThisJobID);
			Message msg = Message.obtain(null, PSOProgramService.MSG_LINKPROGRAM, 0, 0);
			Bundle params = new Bundle();

			//Log.verbose("GFX compile job");
			if (VertexShader.length == 0)
			{
				Log.error("Failed to send compile job VS is null ");
				return null;
			}
			if (PixelShader.length == 0)
			{
				Log.error("Failed to send compile job PS is null ");
				return null;
			}
			params.putByteArray(PSOProgramService.VS_Key, VertexShader);
			params.putByteArray(PSOProgramService.PS_Key, PixelShader);
			params.putByteArray(PSOProgramService.PSOData_Key, PSOData);
			params.putByteArray(PSOProgramService.PSOCacheData_Key, PSOCacheData);


			params.putByteArray(PSOProgramService.JobContext_Key, ContextData);

			params.putInt(PSOProgramService.JobID_Key, ThisJobID);
			params.putLong(PSOProgramService.Priority_Key, PriorityInfo);
			msg.replyTo = mReplyToMe;

			JobResponse pendingResponse = new JobResponse();
			synchronized (pendingResponse.SyncObj)
			{
				SyncObs.put(ThisJobID, pendingResponse);

				msg.setData(params);
				boolean bValidService = false;
				OGLServiceInstance thisInstance = null;
				// loop through the services to find a valid (bound) service, skip and attempt to rebind any unbound services.
				for(int i = 0; i<_PSOProgramServiceAccessor.ServiceInstances.length; i++)
				{
					int ServiceIdx = _PSOProgramServiceAccessor.LastServiceIdx.incrementAndGet() % _PSOProgramServiceAccessor.ServiceInstances.length;
					OGLServiceInstance testInstance = _PSOProgramServiceAccessor.ServiceInstances[ServiceIdx];
					if(testInstance.IsServiceBound())
					{
						// found a valid service.
						thisInstance = testInstance;
						params.putInt(PSOProgramService.ServiceID_Key, ServiceIdx);
						break;
					}
					else
					{
						// found an unbound service, attempt to rebind and continue;
						Log.warn("OGLRemoteProgramLink "+testInstance.Name()+" (" + ThisJobID + ") was unbound, rebinding and trying next service");
						testInstance.doBindService();
						testInstance.ReadBackServiceLog();
					}
				}

				//Log.verbose("OGLRemoteProgramLink dispatching to "+thisInstance.Name()+" (" + ThisJobID + ")");
				pendingResponse.ResponseState = JobResponse.ResponseStateEnum.Pending;
				if( thisInstance != null && thisInstance.SendMessage(msg) )
				{
					int pendingJobs = thisInstance.PendingJobs.incrementAndGet();

					long tStartTimeMS = System.currentTimeMillis();
					// if we're allowed to time out, wait for 10s before giving up on the compile job.
					long tTimeoutInMS = bAllowTimeOuts ? 10000 : Long.MAX_VALUE;

					while (pendingResponse.ResponseState != JobResponse.ResponseStateEnum.Responded)
					{
						// we must loop as waits can randomly wake.
						pendingResponse.SyncObj.wait(1000);

						if ((System.currentTimeMillis() - tStartTimeMS) >= tTimeoutInMS)
						{
							Log.error("OGLRemoteProgramLink TIMED OUT WAITING " + ThisJobID + " for " + (System.currentTimeMillis() - tStartTimeMS) + "ms. pending tasks "+thisInstance.PendingJobs.get());
							//timeout
							SyncObs.remove(ThisJobID);
							thisInstance.ReadBackServiceLog();
							return null;
						}
					}
					long totalWaitTimeMS = (System.currentTimeMillis() - tStartTimeMS);
					if(totalWaitTimeMS > 2500)
					{
						Log.verbose("OGLRemoteProgramLink responded " + ThisJobID + " total wait time "+totalWaitTimeMS+" ms. pending tasks "+thisInstance.PendingJobs.get());
					}
				}
				else if( thisInstance != null)
				{
					// process communications failed somehow.
					SyncObs.remove(ThisJobID);
					thisInstance.ReadBackServiceLog();
					return null;
				}
				else
				{
					// failed to find a single bound service?
					Log.error("OGLRemoteProgramLink " + ThisJobID + " no valid bound services.");
					SyncObs.remove(ThisJobID);
					return null;
				}

			}

			SyncObs.remove(ThisJobID);

			byte[] EngineJobContext = pendingResponse.data.getByteArray(PSOProgramService.JobContext_Key);
			byte[] CompiledBinary = pendingResponse.data.getByteArray(PSOProgramService.CompiledProgram_Key);
			int JobID = pendingResponse.data.getInt(PSOProgramService.JobID_Key);
			float CompilationDuration = pendingResponse.data.getFloat(PSOProgramService.CompilationDuration_Key);

			//Log.verbose("OGLRemoteProgramLink handoff ("+JobID+"), ("+(EngineJobContext == null ? 0 : EngineJobContext.length)+", "+(CompiledBinary==null?0:CompiledBinary.length)+")");

			JNIProgramLinkResponse jniResponse = new JNIProgramLinkResponse();
			String fail = pendingResponse.data.getString(PSOProgramService.JobFail);
			jniResponse.bCompileSuccess = fail == null || fail.isEmpty();
			jniResponse.ErrorMessage = fail;
			jniResponse.CompiledProgram = CompiledBinary;
			jniResponse.CompilationDuration = CompilationDuration;

			return jniResponse;
		}
		catch (Exception e)
		{
			Log.error( "OGLRemoteProgramLink FAIL " + e);
		}
		finally
		{
			ProgramServiceAccessorlock.readLock().unlock();
			endTrace();
		}

		return null;
	}

	private static JNIProgramLinkResponse VKPSOGFXCompileShm_internal(byte[] ContextData, long PriorityInfo, int SharedMemFD, long VertexShaderSize, long PixelShaderSize, long PSODataSize, long PSOCacheDataSize, boolean bAllowTimeOuts)
	{
		ParcelFileDescriptor parcelFD = null;
		try
		{
			ProgramServiceAccessorlock.readLock().lock();
			if (_PSOProgramServiceAccessor == null)
			{
				Log.error("AndroidThunkJava_VKPSOGFXCompileShm Called too early ");
				return null;
			}
			if (_PSOProgramServiceAccessor.ServiceInstances.length == 0)
			{
				Log.error( "AndroidThunkJava_VKPSOGFXCompileShm not enabled.");
				return null;
			}

			int ThisJobID = JobID.incrementAndGet();
			beginTrace("AndroidThunkJava_VKPSOGFXCompileShm " + ThisJobID);
			Message msg = Message.obtain(null, PSOProgramService.MSG_LINKPROGRAM_SHMEM, 0, 0);
			Bundle params = new Bundle();

			//Log.verbose("GFX compile job");
			if (VertexShaderSize == 0)
			{
				Log.error("Failed to send compile job VS is null ");
				return null;
			}
			if (PixelShaderSize == 0)
			{
				Log.error("Failed to send compile job PS is null ");
				return null;
			}

			// Duplicate the incoming file descriptor
			parcelFD = ParcelFileDescriptor.fromFd(SharedMemFD);
			params.putParcelable(PSOProgramService.SHMem_Key, parcelFD );

			params.putLong(PSOProgramService.VS_Key, VertexShaderSize);
			params.putLong(PSOProgramService.PS_Key, PixelShaderSize);
			params.putLong(PSOProgramService.PSOData_Key, PSODataSize);
			params.putLong(PSOProgramService.PSOCacheData_Key, PSOCacheDataSize);

			params.putByteArray(PSOProgramService.JobContext_Key, ContextData);

			params.putInt(PSOProgramService.JobID_Key, ThisJobID);
			params.putLong(PSOProgramService.Priority_Key, PriorityInfo);
			
			msg.replyTo = mReplyToMe;

			JobResponse pendingResponse = new JobResponse();
			synchronized (pendingResponse.SyncObj)
			{
				SyncObs.put(ThisJobID, pendingResponse);

				msg.setData(params);
				boolean bValidService = false;
				OGLServiceInstance thisInstance = null;
				// loop through the services to find a valid (bound) service, skip and attempt to rebind any unbound services.
				for(int i = 0; i<_PSOProgramServiceAccessor.ServiceInstances.length; i++)
				{
					int ServiceIdx = _PSOProgramServiceAccessor.LastServiceIdx.incrementAndGet() % _PSOProgramServiceAccessor.ServiceInstances.length;
					OGLServiceInstance testInstance = _PSOProgramServiceAccessor.ServiceInstances[ServiceIdx];
					if(testInstance.IsServiceBound())
					{
						// found a valid service.
						thisInstance = testInstance;
						params.putInt(PSOProgramService.ServiceID_Key, ServiceIdx);
						break;
					}
					else
					{
						// found an unbound service, attempt to rebind and continue;
						Log.warn("OGLRemoteProgramLink "+testInstance.Name()+" (" + ThisJobID + ") was unbound, rebinding and trying next service");
						testInstance.doBindService();
						testInstance.ReadBackServiceLog();
					}
				}

				if( RetainedProgramInitDataHistoryLimit > 0 )
				{
					// Attempt to capture the input params used to create the PSO
					// this can be useful to gather data required to repro PSO compiler crashes.
					if( thisInstance  != null )
					{
						// Generate the path for the historical creation data. 
						// the filename is in the form: pso_{service_index}_{service_local_job_idx % RetainedProgramInitDataHistoryLimit}
						int ServiceIdx = params.getInt(PSOProgramService.ServiceID_Key);
						String filename = "pso_" +ServiceIdx+"_"+(thisInstance.NumJobsSent % RetainedProgramInitDataHistoryLimit);
						Path FullPath = Paths.get(AndroidThunkJava_GetRetainedProgramInitDataPath(), filename);
						
						CopyFromFileDescriptor(thisInstance.NumJobsSent, ContextData, VertexShaderSize, PixelShaderSize, PSODataSize, PSOCacheDataSize, parcelFD, FullPath);
						thisInstance.NumJobsSent++;
					}
				}

				//Log.verbose("OGLRemoteProgramLink dispatching to "+thisInstance.Name()+" (" + ThisJobID + ")");
				pendingResponse.ResponseState = JobResponse.ResponseStateEnum.Pending;
				if( thisInstance != null && thisInstance.SendMessage(msg) )
				{
					int pendingJobs = thisInstance.PendingJobs.incrementAndGet();

					long tStartTimeMS = System.currentTimeMillis();
					// if we're allowed to time out, wait for 10s before giving up on the compile job.
					long tTimeoutInMS = bAllowTimeOuts ? 10000 : Long.MAX_VALUE;

					while (pendingResponse.ResponseState != JobResponse.ResponseStateEnum.Responded)
					{
						// we must loop as waits can randomly wake.
						pendingResponse.SyncObj.wait(1000);

						boolean bTimedOut = (System.currentTimeMillis() - tStartTimeMS) >= tTimeoutInMS;
						boolean bLostBinding = thisInstance.IsServiceBound() == false; // service crashed?
						if (bTimedOut || bLostBinding)
						{
							if( bTimedOut )
							{
								Log.error("OGLRemoteProgramLink TIMED OUT WAITING " + ThisJobID + " for " + (System.currentTimeMillis() - tStartTimeMS) + "ms. pending tasks "+thisInstance.PendingJobs.get());
							}
							if( bLostBinding )
							{
								Log.error("OGLRemoteProgramLink JOB FAILED - lost binding during " + ThisJobID + ". pending tasks "+thisInstance.PendingJobs.get());
							}

							SyncObs.remove(ThisJobID);
							thisInstance.ReadBackServiceLog();
							return null;
						}
					}
					long totalWaitTimeMS = (System.currentTimeMillis() - tStartTimeMS);
					if(totalWaitTimeMS > 2500)
					{
						Log.verbose("OGLRemoteProgramLink responded " + ThisJobID + " total wait time "+totalWaitTimeMS+" ms. pending tasks "+thisInstance.PendingJobs.get());
					}
				}
				else if( thisInstance != null)
				{
					// process communications failed somehow.
					SyncObs.remove(ThisJobID);
					thisInstance.ReadBackServiceLog();
					return null;
				}
				else
				{
					// failed to find a single bound service?
					Log.error("OGLRemoteProgramLink " + ThisJobID + " no valid bound services.");
					SyncObs.remove(ThisJobID);
					return null;
				}

			}
			SyncObs.remove(ThisJobID);

			byte[] EngineJobContext = pendingResponse.data.getByteArray(PSOProgramService.JobContext_Key);
			
			ParcelFileDescriptor SharedBinaryResponseFD = pendingResponse.data.getParcelable(PSOProgramService.SHMem_Key);
			
			// Detach from the FD, the native code will close it.
			int CompiledBinarySharedFD = SharedBinaryResponseFD.detachFd();

			int JobID = pendingResponse.data.getInt(PSOProgramService.JobID_Key);

			float CompilationDuration = pendingResponse.data.getFloat(PSOProgramService.CompilationDuration_Key);

			//Log.verbose("OGLRemoteProgramLink handoff ("+JobID+"), ("+(EngineJobContext == null ? 0 : EngineJobContext.length)+")");

			JNIProgramLinkResponse jniResponse = new JNIProgramLinkResponse();
			String fail = pendingResponse.data.getString(PSOProgramService.JobFail);
			jniResponse.bCompileSuccess = fail == null || fail.isEmpty();
			jniResponse.ErrorMessage = fail;
			jniResponse.SHMOutputHandle = CompiledBinarySharedFD;
			jniResponse.CompilationDuration = CompilationDuration;

			return jniResponse;
		}
		catch (Exception e)
		{
			Log.error( "AndroidThunkJava_VKPSOGFXCompileShm FAIL ("+JobID+")" + e);
			e.printStackTrace();
		}
		finally
		{
			if(parcelFD != null)
			{
				try
				{
					parcelFD.close();
				}
				catch (IOException e)
				{
					Log.error( "AndroidThunkJava_VKPSOGFXCompileShm FAIL ("+JobID+")" + e);
					e.printStackTrace();
				}
			}
			ProgramServiceAccessorlock.readLock().unlock();
			endTrace();
		}

		return null;
	}

	private static void CopyFromFileDescriptor(long LocalJobID, byte[] ContextData, long VertexShaderSize, long PixelShaderSize, long PSODataSize, long PSOCacheDataSize, ParcelFileDescriptor fd, Path FullPath)
	{
		if( Build.VERSION.SDK_INT < Build.VERSION_CODES.O_MR1)
		{
			return;
		}

		try (SharedMemory shm = SharedMemory.fromFileDescriptor(fd.dup()); FileOutputStream OutStream = new FileOutputStream(String.valueOf(FullPath)); DataOutputStream DataOut = new DataOutputStream(OutStream); FileChannel OutChannel = OutStream.getChannel();)
		{
			DataOut.writeLong(LocalJobID);
			DataOut.writeLong(VertexShaderSize);
			DataOut.writeLong(PixelShaderSize);
			DataOut.writeLong(PSODataSize);
			DataOut.writeLong(PSOCacheDataSize);
			DataOut.writeLong(ContextData.length);
			DataOut.write(ContextData);
			DataOut.flush();

			ByteBuffer DataBuffer = shm.mapReadOnly();
			DataBuffer.position(0);
			while(DataBuffer.hasRemaining())
			{
				OutChannel.write(DataBuffer);
			}
			SharedMemory.unmap(DataBuffer);
		}
		catch (Exception e)
		{
			Log.error("CopyFromFileDescriptor FAILED" + e + " : " + e.getStackTrace());
		}
	}

	class OGLServiceInstance
	{
		private final Class ServiceClass;
		private final boolean bRobustContext;
		private int NumJobsSent = 0;
		boolean mShouldUnbind = false;
		private final AtomicInteger mBound = new AtomicInteger(-1);
		Messenger mService = null;
		private final AtomicInteger PendingJobs = new AtomicInteger(0);

		public String Name()
		{
			return ServiceClass.getSimpleName();
		}

		public OGLServiceInstance(Class ServiceClassIN, boolean bRobustContextIN)
		{
			ServiceClass = ServiceClassIN;
			bRobustContext = bRobustContextIN;
		}

		private final Map<String, Long> LastLogSequencePerUID = new HashMap<>();

		// Opens the log file(s) for the service, scans each entry and prints any new entries.  
		synchronized void ReadBackServiceLog()
		{
			String OGLLogDirPath = mActivity.getFilesDir()+PSOProgramService.LogDir;
			File OGLLogDir = new File(OGLLogDirPath);
			File[] LogFiles = OGLLogDir.listFiles();
			if( LogFiles == null)
			{
				Log.error( "No log files "+Name());
				return;
			}
			for(File LogFile : LogFiles)
			{
				if( LogFile.getName().contains(Name()+"_") && LogFile.getName().endsWith(".txt"))
				{
					try(BufferedReader reader = new BufferedReader(new FileReader(LogFile)))
					{
						for(String line; (line = reader.readLine()) != null; )
						{
							String[] lineid= line.split(",");
							if(lineid.length >= 2)
							{
								String logGuid = lineid[0].trim();
								long logSequence = Long.parseLong(lineid[1].trim());

								Long value = LastLogSequencePerUID.get(logGuid);
								long localLastLogSequence = -1;
								if (value == null)
								{
									LastLogSequencePerUID.put(logGuid, (long) -1);
								}
								else
								{
									localLastLogSequence = value;
								}

								if (logSequence > localLastLogSequence)
								{
									Log.error( Name() +" log :" + line);
									localLastLogSequence = logSequence;
								}
								LastLogSequencePerUID.put(logGuid, localLastLogSequence);
							}
						}
					} catch (NumberFormatException | IOException e)
					{
						Log.error(e.toString());
						e.printStackTrace();
					}
				}
			}
		}

		/**
		 * Defines callbacks for service binding, passed to bindService()
		 */
		private final PSOProgramServiceConnection mConnection = new PSOProgramServiceConnection();

		class PSOProgramServiceConnection implements ServiceConnection
		{
			public final Object mConnectionSync = new Object();

			@Override
			public void onBindingDied (ComponentName name)
			{
				Log.verbose("onBindingDied"+Name());
				synchronized (mConnectionSync)
				{
					mBound.set(0);
				}
				mShouldUnbind = false;
			}

			@Override
			public void onServiceConnected(ComponentName className,
										   IBinder service)
			{
				Log.verbose( "onServiceConnected1 "+Name());
				synchronized (mConnectionSync)
				{
					mService = new Messenger(service);
					mBound.set(1);
					mConnectionSync.notifyAll();
				}
			}

			@Override
			public void onServiceDisconnected(ComponentName arg0)
			{
				Log.verbose("onServiceDisconnected1"+Name());
				synchronized (mConnectionSync)
				{
					mBound.set(0);
				}
				mShouldUnbind = false;
			}
		}

		boolean doBindService()
		{
			Log.verbose("doBindService "+Name());
			if(!mShouldUnbind)
			{
				Intent intent = new Intent(mContext, ServiceClass);

				int BindServiceFlags = Context.BIND_AUTO_CREATE;
				if(	bServicePriHigh )
				{
					BindServiceFlags = BindServiceFlags | Context.BIND_IMPORTANT;
				}

				intent.putExtra(PSOProgramService.RobustContextKey,bRobustContext);

				mShouldUnbind = mContext.bindService(intent, mConnection, BindServiceFlags);

				Log.verbose("doBindService " + Name() + " needs unbind " + mShouldUnbind + " bound: " + mBound+ "robust: "+bRobustContext);
			}
			return mShouldUnbind;
		}
		private long bindStartTime = 0;
		boolean doBind()
		{
			synchronized (mConnection.mConnectionSync)
			{
				boolean bSuccess = doBindService();
				if(bSuccess)
				{
					bindStartTime = SystemClock.uptimeMillis();
				}
				return bSuccess;
			}
		}

		void doUnbindService()
		{
			Log.verbose("doUnbindService"+Name());
			if (mShouldUnbind)
			{
				// Release information about the service's state.
				mContext.unbindService(mConnection);
				mShouldUnbind = false;
				mBound.set(-1);
				bindStartTime = 0;
			}
		}

		boolean IsServiceBound()
		{
			return mBound.get() == 1;
		}

		boolean HasBindFailed()
		{
			return !mShouldUnbind || (!IsServiceBound() && (SystemClock.uptimeMillis()-bindStartTime>10000));
		}

		boolean SendMessage(Message msg) throws RemoteException
		{
			if( mBound.get() == 1)
			{
				mService.send(msg);
				return true;
			}
			else
			{
				Log.error( "cannot SendMessage"+Name()+" service unbound!");
				return false;
			}
		}
	}
}

